//
//  Units.cpp
//  epanet-rtx
//
//  Created by the EPANET-RTX Development Team
//  See README.md and license.txt for more information
//  

#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <deque>
#include "Units.h"
#include "rtxMacros.h"

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>

using namespace RTX;
using namespace std;


Units::Units(double conversion, int mass, int length, int time, int current, int temperature, int amount, int intensity, double offset) {
  _mass         = mass;
  _length       = length;
  _time         = time;
  _current      = current;
  _temperature  = temperature;
  _amount       = amount;
  _intensity    = intensity;
  _conversion   = conversion;
  _offset       = offset;
}

double Units::conversion() const {
  return _conversion;
}


Units Units::operator*(const Units& unit) const {
  
  return Units(_conversion * unit._conversion,
               _mass + unit._mass,
               _length + unit._length,
               _time + unit._time,
               _current + unit._current,
               _temperature + unit._temperature,
               _amount + unit._amount,
               _intensity + unit._intensity);
}

Units Units::operator*(const double factor) const {
  return Units(_conversion * factor,
               _mass, _length, _time, _current, _temperature, _amount, _intensity, _offset);
}


Units Units::operator/(const Units& unit) const {
  
  return Units(_conversion / unit._conversion,
               _mass - unit._mass,
               _length - unit._length,
               _time - unit._time,
               _current - unit._current,
               _temperature - unit._temperature,
               _amount - unit._amount,
               _intensity - unit._intensity);
}


Units Units::operator^(const double power) const {
  int mass = round(_mass*power);
  int length = round(_length*power);
  int time = round(_time*power);
  int current = round(_current*power);
  int temperature = round(_temperature*power);
  int amount = round(_amount*power);
  int intensity = round(_intensity*power);
  
  return Units(pow(_conversion,power), mass, length, time, current, temperature, amount, intensity);
}

bool Units::operator==(const RTX::Units &unit) const {
  if (_conversion == unit._conversion &&
      _mass         == unit._mass         &&
      _length       == unit._length       &&
      _time         == unit._time         &&
      _current      == unit._current      &&
      _temperature  == unit._temperature  &&
      _amount       == unit._amount       &&
      _intensity    == unit._intensity ) {
    return true;
  }
  return false;
}

bool Units::operator!=(const RTX::Units &unit) const {
  return !(*this == unit);
}

bool Units::isSameDimensionAs(const Units& unit) const {
  
  if (_conversion == 0 || unit._conversion == 0) {
    // if no units assigned, can't be same dimension, right?
    return false;
  }
  if (_mass         == unit._mass         &&
      _length       == unit._length       &&
      _time         == unit._time         &&
      _current      == unit._current      &&
      _temperature  == unit._temperature  &&
      _amount       == unit._amount       &&
      _intensity    == unit._intensity ) {
    return true;
  }
  else {
    return false;
  }
}

bool Units::isDimensionless() {
  if (_mass         == 0  &&
      _length       == 0  &&
      _time         == 0  &&
      _current      == 0  &&
      _temperature  == 0  &&
      _amount       == 0  &&
      _intensity    == 0  ) {
    return true;
  }
  else {
    return false;
  }
}


ostream& RTX::operator<< (ostream &out, Units &unit) {
  return unit.toStream(out);
}

ostream& Units::toStream(ostream &stream) {
  if (isDimensionless() && conversion() == 1) {
    stream << "dimensionless";
    return stream;
  }
  else if (isDimensionless() && conversion() == 0) {
    stream << "no_units";
    return stream;
  }
  
  stream << conversion();
  
  if (_mass != 0)    { stream << "*kilograms^"<< _mass; }
  if (_length != 0)  { stream << "*meters^"   << _length; }
  if (_time != 0)    { stream << "*seconds^"  << _time; }
  if (_current != 0) { stream << "*ampere^" << _current; }
  if (_temperature != 0) { stream << "*kelvin^" << _temperature; }
  if (_amount != 0) { stream << "*mole^" << _amount; }
  if (_intensity != 0) { stream << "*candela^" << _intensity; }
  if (_offset != 0) { stream << "*offset^" << _offset; }
  
  return stream;
}


string Units::unitString() {
  map<string, Units> unitMap = Units::unitStringMap;
  
  map<string, Units>::const_iterator it = unitMap.begin();
  
  while (it != unitMap.end()) {
    Units theseUnits = it->second;
    if (theseUnits == (*this)) {
      return it->first;
    } // OR APPROXIMATE UNITS
    else if (theseUnits.isSameDimensionAs(*this) && fabs(theseUnits.conversion() - this->conversion()) < 0.00005 ) {
      return it->first;
    }
    ++it;
  }
  
  stringstream str;
  this->toStream(str);
  string unitsStr = str.str();
  return unitsStr;
  
}



// class methods
double Units::convertValue(double value, const Units& fromUnits, const Units& toUnits) {
  if (fromUnits.isSameDimensionAs(toUnits)) {
    return ((value + fromUnits._offset) * fromUnits._conversion / toUnits._conversion) - toUnits._offset;
  }
  else {
    cerr << "Units are not dimensionally consistent" << endl;
    return 0.;
  }
}


std::map<std::string, Units> Units::unitStringMap = []()
{
  map<string, Units> m;
  
  m["dimensionless"]= RTX_DIMENSIONLESS;
  // pressure
  m["psi"] = RTX_PSI;
  m["pa"]  = RTX_PASCAL;
  m["kpa"] = RTX_KILOPASCAL;
  // distance
  m["ft"]  = RTX_FOOT;
  m["in"]  = RTX_INCH;
  m["m"]   = RTX_METER;
  m["cm"]  = RTX_CENTIMETER;
  // volume
  m["m3"]  = RTX_CUBIC_METER;
  m["gal"] = RTX_GALLON;
  m["mgal"]= RTX_MILLION_GALLON;
  m["liter"]=RTX_LITER;
  m["ft3"] = RTX_CUBIC_FOOT;
  // flow
  m["cms"]= RTX_CUBIC_METER_PER_SECOND;
  m["cfs"] = RTX_CUBIC_FOOT_PER_SECOND;
  m["gps"] = RTX_GALLON_PER_SECOND;
  m["gpm"] = RTX_GALLON_PER_MINUTE;
  m["gpd"] = RTX_GALLON_PER_DAY;
  m["mgd"] = RTX_MILLION_GALLON_PER_DAY;
  m["lps"] = RTX_LITER_PER_SECOND;
  m["lpm"] = RTX_LITER_PER_MINUTE;
  m["mld"] = RTX_MILLION_LITER_PER_DAY;
  m["m3/hr"]=RTX_CUBIC_METER_PER_HOUR;
  m["m3/d"]= RTX_CUBIC_METER_PER_DAY;
  m["acre-ft/d"]=RTX_ACRE_FOOT_PER_DAY;
  m["imgd"]= RTX_IMPERIAL_MILLION_GALLON_PER_DAY;
  // time
  m["s"]   = RTX_SECOND;
  m["min"] = RTX_MINUTE;
  m["hr"]  = RTX_HOUR;
  m["d"]   = RTX_DAY;
  // mass
  m["mg"]  = RTX_MILLIGRAM;
  m["g"]   = RTX_GRAM;
  m["kg"]  = RTX_KILOGRAM;
  // concentration
  m["mg/l"]= RTX_MILLIGRAMS_PER_LITER;
  // conductance
  m["us/cm"]=RTX_MICROSIEMENS_PER_CM;
  // velocity
  m["m/s"] = RTX_METER_PER_SECOND;
  m["fps"] = RTX_FOOT_PER_SECOND;
  m["ft/hr"] = RTX_FOOT_PER_HOUR;
  // acceleration
  m["m/s/s"] = RTX_METER_PER_SECOND_SECOND;
  m["ft/s/s"] = RTX_FOOT_PER_SECOND_SECOND;
  m["ft/hr/hr"] = RTX_FOOT_PER_HOUR_HOUR;

//  m["mgd/s"] = RTX_MILLION_GALLON_PER_DAY_PER_SECOND;
  
  // temperature
  m["kelvin"] = RTX_DEGREE_KELVIN;
  m["rankine"] = RTX_DEGREE_RANKINE;
  m["celsius"] = RTX_DEGREE_CELSIUS;
  m["farenheit"] = RTX_DEGREE_FARENHEIT;
  
  m["kwh"] = RTX_KILOWATT_HOUR;
  m["mj"] = RTX_MEGAJOULE;
  m["j"] = RTX_JOULE;
  
  m["xx-no-units"] = RTX_NO_UNITS;
  m["%"] = RTX_PERCENT;
  
  m["psi-to-ft"] = RTX_FOOT * 2.30665873688 / RTX_PSI;
  
  return m;
}();

// factory for string input
Units Units::unitOfType(const string& unitString) {
  
  if (RTX_STRINGS_ARE_EQUAL(unitString, "")) {
    return RTX_NO_UNITS;
  }
  
  double conversionFactor = 1;
  int mass=0, length=0, time=0, current=0, temperature=0, amount=0, intensity=0, offset=0;
  
  
//  const map<string, Units> unitMap = Units::unitStringMap;
  string uStr = boost::algorithm::to_lower_copy(unitString);
  map<string, Units>::const_iterator found = Units::unitStringMap.find(uStr);
  if (found != Units::unitStringMap.end()) {
    return found->second;
  }
  else {
    // attempt to deserialize the streamed format of the unit conversion and dimension.
    deque<string> components;
    boost::split(components, unitString, boost::is_any_of("*")); // split on multiplier
    
    if (components.size() < 1) {
      cerr << "WARNING: Units not recognized: " << uStr << " - defaulting to NO UNITS." << endl;
      return RTX_NO_UNITS;
    }
    
    // first component will be the unit conversion, so cast that into a number.
    // this may fail, so catch it if so.
    try {
      conversionFactor = boost::lexical_cast<double>(components.front());
    } catch (...) {
      cerr << "WARNING: Units not recognized: " << uStr << "- defaulting to NO UNITS." << endl;
      return RTX_NO_UNITS;
    }
    
    components.pop_front();
    
    // get each dimensional power and set it.
    BOOST_FOREACH(const string& part, components) {
      
      vector<string> dimensionPower;
      boost::split(dimensionPower, part, boost::is_any_of("^"));
      
      int power = boost::lexical_cast<int>(dimensionPower.back());
      dimensionPower.pop_back();
      string dim = dimensionPower.back();
      
      // match the SI dimension name
      if (RTX_STRINGS_ARE_EQUAL(dim, "kilograms")) {
        mass = power;
      } else if (RTX_STRINGS_ARE_EQUAL(dim, "meters")) {
        length = power;
      } else if (RTX_STRINGS_ARE_EQUAL(dim, "seconds")) {
        time = power;
      } else if (RTX_STRINGS_ARE_EQUAL(dim, "ampere")) {
        current = power;
      } else if (RTX_STRINGS_ARE_EQUAL(dim, "kelvin")) {
        temperature = power;
      } else if (RTX_STRINGS_ARE_EQUAL(dim, "mole")) {
        amount = power;
      } else if (RTX_STRINGS_ARE_EQUAL(dim, "candela")) {
        intensity = power;
      } else if (RTX_STRINGS_ARE_EQUAL(dim, "offset")) {
        offset = power;
      }
    }
    
    return Units::Units(conversionFactor, mass, length, time, current, temperature, amount, intensity, offset);
  }
}


