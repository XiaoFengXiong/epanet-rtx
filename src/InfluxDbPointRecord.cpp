#include "InfluxDbPointRecord.h"

#include <boost/asio.hpp>
#include <boost/foreach.hpp>
#include <curl/curl.h>
#include <map>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>



#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

//#include <boost/timer/timer.hpp>

using namespace std;
using namespace RTX;
using boost::asio::ip::tcp;

#define HTTP_OK 200


/*
 
 influx will handle units a litte differently since it doesn't have a straightforward k/v store.
 in each metric name, the format is measurement,tag=value,tag=value[,...]
 we use this tag=value to also store units, but we don't want to expose that to the user on this end.
 influx will keep track of it, but we will have to manually intercept that portion of the name bidirectionally.
 
 */


InfluxDbPointRecord::InfluxDbPointRecord() {
  _connected = false;
  _range = make_pair(0,0);
  _lastIdRequest = time(NULL);
  host = "*HOST*";
  user = "*USER*";
  pass = "*PASS*";
  port = 8086;
  db = "*DB*";
  
}

#pragma mark Connecting

void InfluxDbPointRecord::dbConnect() throw(RtxException) {
  _connected = false;
  this->errorMessage = "Connecting...";
  
  stringstream q;
  q << "/ping?u=" << this->user << "&p=" << this->pass;
  
  JsonDocPtr doc = this->jsonFromPath(q.str());
  if (!doc || doc->IsNull()) {
    cerr << "could not connect" << endl;
    this->errorMessage = "Could Not Connect";
    return;
  }
  else {
    // see if the database needs to be created
    bool dbExists = false;
    q.str("");
    q << "/query?u=" << this->user << "&p=" << this->pass << "&q=" << this->urlEncode("SHOW DATABASES");
    doc = this->jsonFromPath(q.str());
    
    if (doc->IsNull() || !doc->HasMember("results")) {
      this->errorMessage = "SHOW DATABASES failed: Could not get Databases";
      return;
    }
    
    // get list of databases, see if i'm there.
    const rapidjson::SizeType zero = 0;
    const rapidjson::Value& results = (*doc)["results"];
    if (!results.IsArray() || results.Size() == 0) {
      this->errorMessage = "JSON Format Not Recognized";
      return;
    }
    const rapidjson::Value& firstResult = results[zero];
    if (!firstResult.HasMember("series")) {
      this->errorMessage = "JSON Format Not Recognized";
      return;
    }
    
    const rapidjson::Value& series = firstResult["series"];
    if (!series.IsArray() || series.Size() == 0) {
      this->errorMessage = "JSON Format Not Recognized";
      return;
    }
    
    const rapidjson::Value& tsData = series[zero];
    if (tsData.HasMember("values")) {
      // there are databases here.
      const rapidjson::Value& valuesList = tsData["values"];
      if (valuesList.IsArray()) {
        for (rapidjson::SizeType i = 0; i < valuesList.Size(); ++i) {
          string dbName = "";
          // measurement name?
          const rapidjson::Value& thisDbNameRow = valuesList[i];
          if (thisDbNameRow.IsArray() && thisDbNameRow.Size() > 0) {
            // first and only element is the name.
            const rapidjson::Value& dbNameJs = thisDbNameRow[zero];
            dbName = dbNameJs.GetString();
          }
          if (RTX_STRINGS_ARE_EQUAL(dbName, this->db)) {
            dbExists = true;
          }
        }
      }
      
    }
    
    if (!dbExists) {
      // create the database?
      q.str("");
      q << "/query?u=" << this->user << "&p=" << this->pass << "&q=" << this->urlEncode("CREATE DATABASE " + this->db);
      JsonDocPtr doc = this->jsonFromPath(q.str());
      // TODO: handle unable to create db.
    }
    
    
    // made it this far? at least we are connected.
    _connected = true;
    this->errorMessage = "OK";
    
    
    return;
  }
}


string InfluxDbPointRecord::connectionString() {
  
  stringstream ss;
  ss << "host=" << this->host << "&port=" << this->port << "&db=" << this->db << "&u=" << this->user << "&p=" << this->pass;
  
  return ss.str();
}

void InfluxDbPointRecord::setConnectionString(const std::string &str) {
  
  // split the tokenized string. we're expecting something like "host=127.0.0.1&port=4242"
  std::map<std::string, std::string> kvPairs;
  {
    boost::regex kvReg("([^=]+)=([^&]+)&?"); // key - value pair
    boost::sregex_iterator it(str.begin(), str.end(), kvReg), end;
    for ( ; it != end; ++it) {
      kvPairs[(*it)[1]] = (*it)[2];
    }
  }
  
  std::map<std::string, std::string>::iterator notfound = kvPairs.end();
  
  if (kvPairs.find("host") != notfound) {
    this->host = kvPairs["host"];
  }
  if (kvPairs.find("port") != notfound) {
    int intPort = boost::lexical_cast<int>(kvPairs["port"]);
    this->port = intPort;
  }
  if (kvPairs.find("db") != notfound) {
    this->db = kvPairs["db"];
  }
  if (kvPairs.find("u") != notfound) {
    this->user = kvPairs["u"];
  }
  if (kvPairs.find("p") != notfound) {
    this->pass = kvPairs["p"];
  }
  
  
  return;
}


#pragma mark Listing and creating series


bool InfluxDbPointRecord::insertIdentifierAndUnits(const std::string &id, RTX::Units units) {
  
  
  MetricInfo m = InfluxDbPointRecord::metricInfoFromName(id);
  if (m.tags.find("units") != m.tags.end()) {
    m.tags.erase("units"); // get rid of units if they are included.
  }
  string properId = InfluxDbPointRecord::nameFromMetricInfo(m);
  
  if (this->readonly()) {
    // already here. ok if units match. otherwise no-no
    return (_identifiersAndUnitsCache.count(properId) && _identifiersAndUnitsCache[properId] == units);
  }
  
  // otherwise, fine. add the series.
  _identifiersAndUnitsCache[properId] = units;
  
  this->addPoint(id, Point(1,0));
  
  // no futher validation.
  return true;
   
   
   /*
  
  // placeholder in db. insert a point to create the ts, then drop the points (but not the ts)
  
  bool alreadyInIndex = false;
  
  std::map<std::string,Units> existing = this->identifiersAndUnits();
  BOOST_FOREACH( const nameUnitsPair p, existing) {
    string name = p.first;
    Units units = p.second;
    if (RTX_STRINGS_ARE_EQUAL_CS(name,id)) {
      // already here.
      alreadyInIndex = true;
      break;
    }
  }
  
  if (!alreadyInIndex) {
    
    // insert dummy point, then delete it.
    // add the units string if needed.
    
    MetricInfo m = InfluxDbPointRecord::metricInfoFromName(id);
    if (m.tags.find("units") == m.tags.end()) {
      m.tags["units"] = units.unitString();
    }
    string properId = InfluxDbPointRecord::nameFromMetricInfo(m);
    
    _identifiersAndUnitsCache[properId] = units;
    
    this->insertSingle(properId, Point(time(NULL) - 60*60*24*365));
    stringstream ss;
    ss << "delete from " << id << " where time < now()";
    string url = this->urlForQuery(ss.str(),false);
    JsonDocPtr doc = this->jsonFromPath(url);
    
  }
  
  
  return true;
    
    */
}



const std::map<std::string,Units> InfluxDbPointRecord::identifiersAndUnits() {
  
  /*
   
   perform a query to get all the series.
   response will be nested in terms of "measurement", and then each array in the "values" array will denote an individual time series:
   
   series: [
   {   name: flow
   columns:  [asset_id, asset_type, dma, ... ]
   values: [ [33410,    pump,       brecon, ...],
   [33453,    pipe,       mt.\ washington, ...],
   [...]
   ]
   },
   {   name: pressure
   columns:   [asset_id, asset_type, dma, ...]
   values: [  [44305,    junction,   brecon, ...],
   [43205,    junction,   mt.\ washington, ...],
   [...]
   ]
   }
   
   */
  
  // quick cache hit. 5-second validity window.
  time_t now = time(NULL);
  if (now - _lastIdRequest < 5 && !_identifiersAndUnitsCache.empty()) {
    return DbPointRecord::identifiersAndUnits();
  }
  _lastIdRequest = now;
  
  _identifiersAndUnitsCache.clear();
  
  if (!this->isConnected()) {
    this->dbConnect();
  }
  if (!this->isConnected()) {
    return _identifiersAndUnitsCache;
  }
  
  string q = "show series";
  string url = this->urlForQuery(q,false);
  JsonDocPtr js = this->jsonFromPath(url);
  
  if (js) {
    
    if (js->IsNull() || !js->HasMember("results")) {
      return _identifiersAndUnitsCache;
    }
    const rapidjson::SizeType zero = 0;
    const rapidjson::Value& results = (*js)["results"];
    if (!results.IsArray() || results.Size() == 0) {
      return _identifiersAndUnitsCache;
    }
    const rapidjson::Value& rzero = results[zero];
    if(!rzero.IsObject() || !rzero.HasMember("series")) {
      return _identifiersAndUnitsCache;
    }
    const rapidjson::Value& series = rzero["series"];
    if (!series.IsArray() || series.Size() == 0) {
      return _identifiersAndUnitsCache;
    }
    for (rapidjson::SizeType i = 0; i < series.Size(); ++i) {
      // measurement name?
      MetricInfo m;
      const rapidjson::Value& thisSeries = series[i];
      m.measurement = thisSeries["name"].GetString();
      const rapidjson::Value& columns = thisSeries["columns"];
      const rapidjson::Value& valuesArr = thisSeries["values"];
      // valuesArr is an array of arrays.
      for (rapidjson::SizeType iVal = 0; iVal < valuesArr.Size(); ++iVal) {
        
        // this is where a time series is defined!
        
        const rapidjson::Value& thisTsValues = valuesArr[iVal];
        // size of thisTsValues should == size of columns.
        for (rapidjson::SizeType j = 0; j < thisTsValues.Size(); ++j) {
          const string tsKeyStr = columns[j].GetString();
          const string tsValStr = thisTsValues[j].GetString();
          
          // exclude internal influx _key:
          if (RTX_STRINGS_ARE_EQUAL(tsKeyStr, "_key")) {
            continue;
          }
          
          // exclude empty valued keys
          if (RTX_STRINGS_ARE_EQUAL(tsValStr, "")) {
            continue;
          }
          
          m.tags[tsKeyStr] = tsValStr;
        }
        
        // now we have all kv pairs that define a time series.
        // do we have units info? strip it off before showing the user.
        Units units = RTX_NO_UNITS;
        if (m.tags.find("units") != m.tags.end()) {
          units = Units::unitOfType(m.tags["units"]);
          // remove units from string name.
          m.tags.erase("units");
        }
        
        // now assemble the complete name:
        string properId = InfluxDbPointRecord::nameFromMetricInfo(m);
        
        // the name has been assembled!
        _identifiersAndUnitsCache[properId] = units;
        
      } // for each values array (ts definition)
    } // for each measurement
  } // if js body exists
  
  return _identifiersAndUnitsCache;
}




InfluxDbPointRecord::MetricInfo InfluxDbPointRecord::metricInfoFromName(const std::string &name) {
  
  MetricInfo m;
  size_t firstComma = name.find(",");
  // measure name is everything up to the first comma, even if that's everything
  m.measurement = name.substr(0,firstComma);
  
  if (firstComma != string::npos) {
    // a comma was found. therefore treat the name as tokenized
    string keysValuesStr = name.substr(firstComma+1);
    boost::regex kvReg("([^=]+)=([^,]+),?"); // key - value pair
    boost::sregex_iterator it(keysValuesStr.begin(), keysValuesStr.end(), kvReg), end;
    for ( ; it != end; ++it) {
      m.tags[(*it)[1]] = (*it)[2];
    }
  }
  return m;
}

const string InfluxDbPointRecord::nameFromMetricInfo(RTX::InfluxDbPointRecord::MetricInfo info) {
  stringstream ss;
  ss << info.measurement;
  typedef pair<string,string> stringPair;
  BOOST_FOREACH( stringPair p, info.tags) {
    ss << "," << p.first << "=" << p.second;
  }
  const string name = ss.str();
  return name;
}

std::string InfluxDbPointRecord::properId(const std::string& id) {
  return InfluxDbPointRecord::nameFromMetricInfo(InfluxDbPointRecord::metricInfoFromName(id));
}


string InfluxDbPointRecord::_influxIdForTsId(const string& id) {
  // put named keys in proper order...
  MetricInfo m = InfluxDbPointRecord::metricInfoFromName(id);
  if (m.tags.count("units")) {
    m.tags.erase("units");
  }
  string tsId = InfluxDbPointRecord::nameFromMetricInfo(m);
  
  if (_identifiersAndUnitsCache.find(tsId) == _identifiersAndUnitsCache.end()) {
    cerr << "no registered ts with that id: " << tsId << endl;
    return "";
  }
  
  Units u = _identifiersAndUnitsCache[tsId];
  m.tags["units"] = u.unitString();
  string dbId = InfluxDbPointRecord::nameFromMetricInfo(m);
  return dbId;
}





#pragma mark SELECT


std::vector<Point> InfluxDbPointRecord::selectRange(const std::string& id, time_t startTime, time_t endTime) {
  std::vector<Point> points;
  
  string dbId = _influxIdForTsId(id);
  
  DbPointRecord::Query q = this->queryPartsFromMetricId(dbId);
  q.where.push_back("time >= " + to_string(startTime) + "s");
  q.where.push_back("time <= " + to_string(endTime) + "s");
  
  string url = this->urlForQuery(q.selectStr());
  
  JsonDocPtr doc = this->jsonFromPath(url);
  return this->pointsFromJson(doc);
}


Point InfluxDbPointRecord::selectNext(const std::string& id, time_t time) {
  std::vector<Point> points;
  string dbId = _influxIdForTsId(id);
  DbPointRecord::Query q = this->queryPartsFromMetricId(dbId);
  q.where.push_back("time > " + to_string(time) + "s");
  q.order = "time asc limit 1";
  
  string url = this->urlForQuery(q.selectStr());
  JsonDocPtr doc = this->jsonFromPath(url);
  points = this->pointsFromJson(doc);
  
  if (points.size() == 0) {
    return Point();
  }
  
  return points.front();
}


Point InfluxDbPointRecord::selectPrevious(const std::string& id, time_t time) {
  std::vector<Point> points;
  string dbId = _influxIdForTsId(id);
  DbPointRecord::Query q = this->queryPartsFromMetricId(dbId);
  q.where.push_back("time < " + to_string(time) + "s");
  q.order = "time desc limit 1";
  
  string url = this->urlForQuery(q.selectStr());
  JsonDocPtr doc = this->jsonFromPath(url);
  points = this->pointsFromJson(doc);
  
  if (points.size() == 0) {
    return Point();
  }
  
  return points.front();
}


#pragma mark INSERT

void InfluxDbPointRecord::insertSingle(const std::string& id, Point point) {
  
  vector<Point> points;
  points.push_back(point);
  
  this->insertRange(id, points);
  
}

void InfluxDbPointRecord::insertRange(const std::string& id, std::vector<Point> points) {
  vector<Point> insertionPoints;
  string dbId = _influxIdForTsId(id);
  
  if (points.size() == 0) {
    return;
  }
  
  vector<Point> existing;
  existing = this->selectRange(dbId, points.front().time - 1, points.back().time + 1);
  map<time_t,bool> existingMap;
  BOOST_FOREACH(const Point& p, existing) {
    existingMap[p.time] = true;
  }
  
  BOOST_FOREACH(const Point& p, points) {
    if (existingMap.find(p.time) == existingMap.end()) {
      insertionPoints.push_back(p);
    }
  }
  
  if (insertionPoints.size() == 0) {
    return;
  }
  
  const string content = this->insertionDataFromPoints(dbId, insertionPoints);
  this->sendPointsWithString(content);
  
  // cache the inserted range.
  BOOST_FOREACH(const Point& p, insertionPoints) {
    if (p.time > _range.second) {
      _range.second = p.time;
    }
    if (p.time < _range.first || _range.first == 0) {
      _range.first = p.time;
    }
  }
  
}


#pragma mark DELETE

void InfluxDbPointRecord::removeRecord(const std::string& id) {
  // to-do fix this. influx bug related to dropping a series:
  return;
  
  DbPointRecord::Query q = this->queryPartsFromMetricId(id);
  
  stringstream sqlss;
  sqlss << "DROP SERIES FROM " << q.nameAndWhereClause();
  string url = this->urlForQuery(sqlss.str(),false);
  
  JsonDocPtr doc = this->jsonFromPath(url);
}

void InfluxDbPointRecord::truncate() {
  
  stringstream truncateSS;
  truncateSS << "/query?u=" << this->user << "&p=" << this->pass << "&q=" << this->urlEncode("DROP DATABASE " + this->db);
  JsonDocPtr d = this->jsonFromPath(truncateSS.str());
  
  // reconnecting will re-create the database.
  this->dbConnect();
}




#pragma mark Query Building
DbPointRecord::Query InfluxDbPointRecord::queryPartsFromMetricId(const std::string& name) {
  MetricInfo m = InfluxDbPointRecord::metricInfoFromName(name);
  
  DbPointRecord::Query q;
  
  q.from = "\"" + m.measurement + "\"";
  
  typedef pair<string,string> stringPair;
  if (m.tags.size() > 0) {
    BOOST_FOREACH( stringPair p, m.tags) {
      stringstream ss;
      ss << p.first << "='" << p.second << "'";
      q.where.push_back(ss.str());
    }
  }
  
  return q;
}



const std::string InfluxDbPointRecord::urlEncode(std::string s) {
  
  std::string encStr("");
  
  CURL *curl;
  curl = curl_easy_init();
  if (curl) {
    char *enc;
    enc = curl_easy_escape(curl, s.c_str(), 0);
    encStr = string(enc);
    curl_easy_cleanup(curl);
  }
  
  //  cout << s << endl;
  return encStr;
}


const string InfluxDbPointRecord::urlForQuery(const std::string& query, bool appendTimePrecision) {
  stringstream queryss;
  queryss << "/query?db=" << this->db;
  queryss << "&u=" << this->user;
  queryss << "&p=" << this->pass;
  queryss << "&q=" << this->urlEncode(query);
  if (appendTimePrecision) {
    queryss << "&epoch=s";
  }
  
  return queryss.str();
}


#pragma mark Parsing

JsonDocPtr InfluxDbPointRecord::jsonFromPath(const std::string &url) {
  JsonDocPtr documentOut;
  InfluxConnectInfo_t connectionInfo;
  
  // set a timeout for socket connection operations.
  connectionInfo.sockStream.expires_from_now(boost::posix_time::seconds(20));
  
  connectionInfo.sockStream.connect(this->host, to_string(this->port));
  if (!connectionInfo.sockStream) {
    cerr << "influx cannot connect" << endl;
    return documentOut;
  }
  
  string body;
  {
    // TX
    connectionInfo.sockStream << "GET " << url << " HTTP/1.0\r\n";
    connectionInfo.sockStream << "Host: " << this->host << "\r\n";
    connectionInfo.sockStream << "Accept: */*\r\n";
    connectionInfo.sockStream << "Connection: close\r\n\r\n";
    connectionInfo.sockStream.flush();
    
    // RX
    connectionInfo.sockStream >> connectionInfo.httpVersion;
    connectionInfo.sockStream >> connectionInfo.statusCode;
    getline(connectionInfo.sockStream, connectionInfo.statusMessage);
    
    string headerStr;
    connectionInfo.sockStream >> headerStr;
    while (std::getline(connectionInfo.sockStream, headerStr) && headerStr != "\r") {/* nothing */}
    
    std::getline(connectionInfo.sockStream, body);
    cout << connectionInfo.sockStream.rdbuf() << endl;
    
    if (connectionInfo.statusCode != 204 && !connectionInfo.sockStream) {
      std::cerr << "Influx Connection Error: " << connectionInfo.statusMessage << "\n";
    }
    connectionInfo.sockStream.close();
  }
  
  documentOut.reset(new rapidjson::Document);
  if (connectionInfo.statusCode == 204 /* no content but request OK*/) {
    documentOut.get()->Parse<0>("{}");
    return documentOut;
  }
  
  documentOut.get()->Parse<0>(body.c_str());
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  documentOut->Accept(writer);
  return documentOut;
}

vector<Point> InfluxDbPointRecord::pointsFromJson(JsonDocPtr doc) {
  vector<Point> points;
  
  // multiple time series might be returned eventually, but for now it's just a single-value array.
  
  if (doc == NULL || !doc->IsObject()) {
    return points;
  }

  if (!doc->HasMember("results")) {
    return points;
  }
  
  const rapidjson::SizeType zero = 0;
  const rapidjson::Value& results = (*doc)["results"];
  if (!results.IsArray() || results.Size() == 0) {
    return points;
  }
  
  const rapidjson::Value& rzero = results[zero];
  if(!rzero.IsObject() || !rzero.HasMember("series")) {
    return points;
  }
  
  const rapidjson::Value& series = rzero["series"];
  if (!series.IsArray() || series.Size() == 0) {
    return points;
  }
  
  const rapidjson::Value& tsData = series[zero];
  string measureName = tsData["name"].GetString();
  
  // create a little map so we know what order the columns are in
  map<string,int> columnMap;
  const rapidjson::Value& columns = tsData["columns"];
  for (rapidjson::SizeType i = 0; i < columns.Size(); ++i) {
    string colName = columns[i].GetString();
    columnMap[colName] = (int)i;
  }
  
  int timeIndex = columnMap["time"];
  int valueIndex = columnMap["value"];
  int qualityIndex = columnMap["quality"];
  int confidenceIndex = columnMap["confidence"];
  
  // now go through each returned row and create a point.
  // use the column name map to set point properties.
  const rapidjson::Value& pointRows = tsData["values"];
  if (!pointRows.IsArray() || pointRows.Size() == 0) {
    return points;
  }
  
  points.reserve((size_t)pointRows.Size());
  for (rapidjson::SizeType i = 0; i < pointRows.Size(); ++i) {
    const rapidjson::Value& row = pointRows[i];
    time_t pointTime = (time_t)row[timeIndex].GetInt();
    double pointValue = row[valueIndex].GetDouble();
    Point::PointQuality pointQuality = (row[qualityIndex].IsNull()) ? Point::opc_rtx_override : (Point::PointQuality)row[qualityIndex].GetInt();
    double pointConf = row[confidenceIndex].GetDouble();
    Point p(pointTime, pointValue, pointQuality, pointConf);
    points.push_back(p);
  }
  
  
  
  return points;
}





const string InfluxDbPointRecord::insertionDataFromPoints(const string& tsName, vector<Point> points) {
  
  /*
   As you can see in the example below, you can post multiple points to multiple series at the same time by separating each point with a new line. Batching points in this manner will result in much higher performance.
   
   curl -i -XPOST 'http://localhost:8086/write?db=mydb' --data-binary '
   cpu_load_short,host=server01,region=us-west value=0.64
   cpu_load_short,host=server02,region=us-west value=0.55 1422568543702900257 
   cpu_load_short,direction=in,host=server01,region=us-west value=23422.0 1422568543702900257'
   
   */
  
  
  stringstream ss;
  
  int i = 0;
  BOOST_FOREACH(const Point& p, points) {
    if (i++ > 0) {
      ss << '\n';
    }
    string valueStr = to_string(p.value);
    // test for no-decimal condition
    // does this ever evaluate to true? or is to_string well-behaved for this application?
    if ( p.value == double(int(p.value)) && valueStr.find(".") == string::npos) {
      valueStr.append(".");
    }
    
    ss << tsName << " value=" << valueStr << "," << "quality=" << (int)p.quality << "," << "confidence=" << p.confidence << " " << p.time;
  }
  
  string data = ss.str();
  return data;
}


void InfluxDbPointRecord::sendPointsWithString(const string& content) {
  
  
  // host:port/write?db=my-db&precision=s
  
  stringstream queryss;
  queryss << "/write?db=" << this->db;
  queryss << "&u=" << this->user;
  queryss << "&p=" << this->pass;
  queryss << "&precision=s";
  
  string url("http://localhost:8086");
  
  url.append(queryss.str());
  
  
  InfluxConnectInfo_t connectionInfo;
  connectionInfo.sockStream.connect(this->host, to_string(this->port));
  if (!connectionInfo.sockStream) {
    cerr << "influx cannot connect" << endl;
    return;
  }
  
  stringstream httpContent;
  
  httpContent << "POST " << url << " HTTP/1.0\r\n";
  httpContent << "Host: " << this->host << "\r\n";
  httpContent << "Accept: */*\r\n";
  httpContent << "Content-Type: text/plain\r\n";
  httpContent << "Content-Length: " << content.length() << "\r\n";
  httpContent << "Connection: close\r\n\r\n";
  httpContent << content;
  httpContent.flush();
  
  // send the data
  connectionInfo.sockStream << httpContent.str();
  
  // get response, process headers
  connectionInfo.sockStream >> connectionInfo.httpVersion;
  connectionInfo.sockStream >> connectionInfo.statusCode;
  getline(connectionInfo.sockStream, connectionInfo.statusMessage);
  
  string body;
  string headerStr;
  while (std::getline(connectionInfo.sockStream, headerStr) && headerStr != "\r") {
    //cout << headerStr << endl;
    // nothing
  }
  
  
  std::getline(connectionInfo.sockStream, body);
  cout << connectionInfo.sockStream.rdbuf() << endl;
  connectionInfo.sockStream.flush();
  connectionInfo.sockStream.close();
}


