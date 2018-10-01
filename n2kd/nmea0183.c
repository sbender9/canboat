/*

Convert JSON encoded NMEA2000 PGNs to NMEA0183.

At this moment it only supports what one of the authors needed: sensor data
other than GPS and AIS: depth, heading, wind.

(C) 2009-2013, Kees Verruijt, Harlingen, The Netherlands.

This file is part of CANboat.

CANboat is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

CANboat is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with CANboat.  If not, see <http://www.gnu.org/licenses/>.

*/


#include "common.h"
#include <math.h>

#include "gps_ais.h"
#include "n2kd.h"
#include "nmea0183.h"

extern char * srcFilter;
extern bool rateLimit;

/*
 * Which PGNs do we care and know about for now?
 *
 * NMEA 0183 information from the excellent reference at
 * www.catb.org/gpsd/NMEA.txt
 *
 * PGN 127250 "Vessel Heading" -> $xxHDG
 * PGN 130306 "Wind Data"      -> $xxMWV
 * PGN 128267 "Water Depth"    -> $xxDBK/DBS/DBT
 * PGN 128267 "Water Speed"    -> $xxVHW
 * PGN 127245 "Rudder"         -> $xxRSA
 * PGN 130311 "Environmental Parameters - water temperature" -> $xxMTW
 * PGN 128275 "Distance Log"   -> $xxVLW

 * Some others are in gps_ais.c file
 * PGN 129026 "Track made good and Ground speed" -> $xxVTG
 * PGN 129539 "GPS DOP"                          -> $xxGSA
 * PGN 129025 or 129029 "GPS Position"           -> $xxGLL
 * PGN 129038 and 129039 "AIS from other boats"  -> !AIVDM - NOT FINISHED! AIVDM/AIVDO protocol encoding is needed

 * Typical output of these from analyzer:
 * {"timestamp":"2010-09-12-10:57:41.217","prio":"2","src":"36","dst":"255","pgn":"127250","description":"Vessel Heading","fields":{"SID":"116","Heading":"10.1","Deviation":"0.0","Variation":"0.0","Reference":"Magnetic"}}
 * {"timestamp":"2010-09-12-11:00:20.269","prio":"2","src":"13","dst":"255","pgn":"130306","description":"Wind Data","fields":{"Wind Speed":"5.00","Wind Angle":"308.8","Reference":"Apparent"}}
 * {"timestamp":"2012-12-01-12:53:19.929","prio":"3","src":"35","dst":"255","pgn":"128267","description":"Water Depth","fields":{"SID":"70","Depth":"0.63","Offset":"0.500"}}
 * {"timestamp":"2015-12-07-21:51:11.381","prio":"2","src":"4","dst":"255","pgn":"128259","description":"Speed","fields":{"Speed Water Referenced":0.30}}
 * {"timestamp":"2015-12-09-21:53:47.497","prio":"2","src":"1","dst":"255","pgn":"127245","description":"Rudder","fields":{"Angle Order":-0.0,"Position":6.8}}
 * {"timestamp":"2015-12-11T17:56:55.755Z","prio":6,"src":2,"dst":255,"pgn":129539,"description":"GNSS DOPs","fields":{"SID":239,"Desired Mode":"3D","Actual Mode":"3D","HDOP":1.21,"VDOP":1.83,"TDOP":327.67}}
 * {"timestamp":"2016-04-14T20:27:02.303Z","prio":5,"src":35,"dst":255,"pgn":130311,"description":"Environmental Parameters","fields":{"SID":222,"Temperature Source":"Sea Temperature","Temperature":17.16}}
 * {"timestamp":"2016-04-20T21:03:57.631Z","prio":6,"src":35,"dst":255,"pgn":128275,"description":"Distance Log","fields":{"Log":57688,"Trip Log":57688}}
 */

#define PGN_VESSEL_HEADING (127250)
#define PGN_WIND_DATA      (130306)
#define PGN_WATER_DEPTH    (128267)
#define PGN_WATER_SPEED    (128259)
#define PGN_ENVIRONMENTAL  (130311)
#define PGN_DISTANCE_LOG   (128275)
#define PGN_RUDDER         (127245)
#define PGN_SOG_COG        (129026)
#define PGN_GPS_DOP        (129539)
#define PGN_POSITION       (129029)
#define PGN_AIS_A          (129038)
#define PGN_AIS_B          (129039)

enum
{
  RATE_NO_LIMIT = -1,
  RATE_VESSEL_HEADING = 0,
  RATE_WIND_DATA,
  RATE_WATER_DEPTH,
  RATE_WATER_SPEED,
  RATE_RUDDER,
  RATE_GPS_SPEED,
  RATE_GPS_DOP,
  RATE_GPS_POSITION,
  RATE_ENVIRONMENTAL,
  RATE_DISTANCE_LOG,
  RATE_COUNT
};

static int64_t rateLimitPassed[256][RATE_COUNT];

void nmea0183CreateMessage( StringBuffer * msg183, int src, const char * format, ...)
{
  unsigned int chk;
  size_t i;
  char first, second;
  va_list ap;

  va_start(ap, format);

  // Convert the 8 bit value 'n' into a valid NMEA0183 style sender.
  // The first implementation sent out a 2 digit hexadecimal number,
  // but that throws some implementations of receivers off as they
  // cannot handle numeric senders. So now we produce a 2 character
  // code with the src value 0-255 translated into
  // A..Q A..P with A representing 0, B representing 1, etc.
  // P is skipped for the initial letter, as that represents 'proprietary'
  // and OpenCPN does not allow this.

  first  = 'A' + ((src >> 4) & 0xf);
  second = 'A' + ((src     ) & 0xf);
  if (first >= 'P')
  {
    first++;
  }

  sbAppendFormat(msg183, "$%c%c", first, second);
  sbAppendFormatV(msg183, format, ap);

  va_end(ap);

  chk = 0;
  for (i = 1; i < msg183->len; i++)
  {
    chk ^= (unsigned int) msg183->data[i];
  }
  sbAppendFormat(msg183, "*%02X\r\n", chk);
  logDebug("nmea0183 = %s", sbGet(msg183));
}

/*

=== HDG - Heading - Deviation & Variation ===

------------------------------------------------------------------------------
        1   2   3 4   5 6
        |   |   | |   | |
 $--HDG,x.x,x.x,a,x.x,a*hh<CR><LF>
------------------------------------------------------------------------------

Field Number:

1. Magnetic Sensor heading in degrees
2. Magnetic Deviation, degrees
3. Magnetic Deviation direction, E = Easterly, W = Westerly
4. Magnetic Variation degrees
5. Magnetic Variation direction, E = Easterly, W = Westerly
6. Checksum


=== HDM - Heading - Magnetic ===

Vessel heading in degrees with respect to magnetic north produced by
any device or system producing magnetic heading.

------------------------------------------------------------------------------
        1   2 3
        |   | |
 $--HDM,x.x,M*hh<CR><LF>
------------------------------------------------------------------------------

Field Number:

1. Heading Degrees, magnetic
2. M = magnetic
3. Checksum

=== HDT - Heading - True ===

Actual vessel heading in degrees true produced by any device or system
producing true heading.

------------------------------------------------------------------------------
        1   2 3
        |   | |
 $--HDT,x.x,T*hh<CR><LF>
------------------------------------------------------------------------------

Field Number:

1. Heading Degrees, true
2. T = True
3. Checksum
*/
static void nmea0183VesselHeading( StringBuffer * msg183, int src, const char * msg )
{
  char headingString[30];
  char deviationString[30];
  char variationString[30];
  char referenceString[30];

  if (getJSONValue(msg, "Heading", headingString, sizeof(headingString))
   && getJSONValue(msg, "Reference", referenceString, sizeof(referenceString)))
  {
    if (getJSONValue(msg, "Deviation", deviationString, sizeof(deviationString))
     && getJSONValue(msg, "Variation", variationString, sizeof(variationString))
     && strcmp(referenceString, "Magnetic") == 0)
    {
      /* Enough info for HDG message */
      double dev = strtod(deviationString, 0);
      double var = strtod(variationString, 0);

      nmea0183CreateMessage(msg183, src, "HDG,%s,%04.1f,%c,%04.1f,%c"
                           , headingString
                           , fabs(dev)
                           , ((dev < 0.0) ? 'W' : 'E')
                           , fabs(var)
                           , ((var < 0.0) ? 'W' : 'E')
                           );
    }
    else if (strcmp(referenceString, "True") == 0)
    {
      nmea0183CreateMessage(msg183, src, "HDT,%s,T", headingString);
    }
    else if (strcmp(referenceString, "Magnetic") == 0)
    {
      nmea0183CreateMessage(msg183, src, "HDM,%s,M", headingString);
    }
  }
}

/*
=== MWV - Wind Speed and Angle ===

------------------------------------------------------------------------------
        1   2 3   4 5 6
        |   | |   | | |
 $--MWV,x.x,a,x.x,a,a*hh<CR><LF>
------------------------------------------------------------------------------

Field Number:

1. Wind Angle, 0 to 360 degrees
2. Reference, R = Relative, T = True
3. Wind Speed
4. Wind Speed Units, K/M/N
5. Active (A) or invalid (V)
6. Checksum
*/

static void nmea0183WindData( StringBuffer * msg183, int src, const char * msg )
{
  char speedString[30];
  char angleString[30];
  char referenceString[30];

  if (getJSONValue(msg, "Wind Speed", speedString, sizeof(speedString))
   && getJSONValue(msg, "Wind Angle", angleString, sizeof(angleString))
   && getJSONValue(msg, "Reference", referenceString, sizeof(referenceString)))
  {
    double speed = strtod(speedString, 0);

    if (strcmp(referenceString, "True") == 0)
    {
      nmea0183CreateMessage(msg183, src, "MWV,%s,T,%.1f,K,A", angleString, SPEED_M_S_TO_KMH(speed));
      nmea0183CreateMessage(msg183, src, "MWD,,T,%s,M,%.1f,N,%.1f,M", angleString, SPEED_M_S_TO_KNOTS(speed), speed);
    }
    else if (strcmp(referenceString, "Apparent") == 0)
    {
      nmea0183CreateMessage(msg183, src, "MWV,%s,R,%.1f,K,A", angleString, SPEED_M_S_TO_KMH(speed));
    }
  }
}

/*
=== DBK - Depth Below Keel ===

------------------------------------------------------------------------------
        1   2 3   4 5   6 7
        |   | |   | |   | |
 $--DBK,x.x,f,x.x,M,x.x,F*hh<CR><LF>
------------------------------------------------------------------------------

Field Number:

1. Depth, feet
2. f = feet
3. Depth, meters
4. M = meters
5. Depth, Fathoms
6. F = Fathoms
7. Checksum

=== DBS - Depth Below Surface ===

------------------------------------------------------------------------------
        1   2 3   4 5   6 7
        |   | |   | |   | |
 $--DBS,x.x,f,x.x,M,x.x,F*hh<CR><LF>
------------------------------------------------------------------------------

Field Number:

1. Depth, feet
2. f = feet
3. Depth, meters
4. M = meters
5. Depth, Fathoms
6. F = Fathoms
7. Checksum

=== DBT - Depth below transducer ===

------------------------------------------------------------------------------
        1   2 3   4 5   6 7
        |   | |   | |   | |
 $--DBT,x.x,f,x.x,M,x.x,F*hh<CR><LF>
------------------------------------------------------------------------------

Field Number:

1. Depth, feet
2. f = feet
3. Depth, meters
4. M = meters
5. Depth, Fathoms
6. F = Fathoms
7. Checksum

 * {"timestamp":"2012-12-01-12:53:19.929","prio":"3","src":"35","dst":"255","pgn":"128267","description":"Water Depth","fields":{"SID":"70","Depth":"0.63","Offset":"0.500"}}
 */
static void nmea0183WaterDepth( StringBuffer * msg183, int src, const char * msg )
{
  char depthString[30];
  char offsetString[30];

  if (getJSONValue(msg, "Depth", depthString, sizeof(depthString))
   && getJSONValue(msg, "Offset", offsetString, sizeof(offsetString)))
  {
    double off = strtod(offsetString, 0);
    double dep = strtod(depthString, 0);

    nmea0183CreateMessage(msg183, src, "DPT,%04.1f,%04.1f", dep, off);
  }
}

/*

=== VHW - Water speed and heading ===
------------------------------------------------------------------------------
        1   2 3   4 5   6 7   8 9
        |   | |   | |   | |   | |
 $--VHW,x.x,T,x.x,M,x.x,N,x.x,K*hh<CR><LF>
------------------------------------------------------------------------------

Field Number:

1. Degress True
2. T = True
3. Degrees Magnetic
4. M = Magnetic
5. Knots (speed of vessel relative to the water)
6. N = Knots
7. Kilometers (speed of vessel relative to the water)
8. K = Kilometers
9. Checksum
*/

static void nmea0183WaterSpeed( StringBuffer * msg183, int src, const char * msg )
{
  char speedString[30];

  if (getJSONValue(msg, "Speed Water Referenced", speedString, sizeof(speedString)))
  {
    double speed = strtod(speedString, 0);

    nmea0183CreateMessage(msg183, src, "VHW,,T,,M,%04.1f,N,%04.1f,K", SPEED_M_S_TO_KNOTS(speed), SPEED_M_S_TO_KMH(speed));
  }
}

/*

=== MTW - Mean Temperature of Water ===
------------------------------------------------------------------------------
$--MTW,x.x,C*hh<CR><LF>
------------------------------------------------------------------------------

Field Number:
1. Degrees
2. Unit of Measurement, Celcius
3. Checksum
*/

static void nmea0183WaterTemperature( StringBuffer * msg183, int src, const char * msg )
{
  char temperatureString[30];
  char sourceString[30];

  if (getJSONValue(msg, "Temperature Source", sourceString, sizeof(sourceString))
   && (strcmp(sourceString, "Sea Temperature") == 0)
   && getJSONValue(msg, "Temperature", temperatureString, sizeof(temperatureString)))
  {
    double temp = strtod(temperatureString, 0);

    nmea0183CreateMessage(msg183, src, "MTW,%04.1f,C", TEMP_K_TO_C(temp));
  }

}

/*
VLW - Distance Traveled through Water
------------------------------------------------------------------------------
       1   2 3   4 5
       |   | |   | |
$--VLW,x.x,N,x.x,N*hh<CR><LF>
------------------------------------------------------------------------------

Field Number:
1. Total cumulative distance
2. N = Nautical Miles
3. Distance since Reset
4. N = Nautical Miles
5. Checksum
*/

static void nmea0183DistanceTraveled( StringBuffer * msg183, int src, const char * msg )
{
  char logString[30];
  char tripString[30];

  if (getJSONValue(msg, "Log", logString, sizeof(logString))
   && getJSONValue(msg, "Trip Log", tripString, sizeof(tripString)))
  {
    double total = strtod(logString, 0);
    double trip = strtod(tripString, 0);

    nmea0183CreateMessage(msg183, src, "VLW,%.1f,N,%.1f,N", DIST_M_TO_NM(total), DIST_M_TO_NM(trip));
  }
}

/*
=== RSA - Rudder Sensor Angle ===
------------------------------------------------------------------------------
        1   2 3   4 5
        |   | |   | |
 $--RSA,x.x,A,x.x,A*hh<CR><LF>
------------------------------------------------------------------------------

Field Number:

1. Starboard (or single) rudder sensor, "-" means Turn To Port
2. Status, A means data is valid
3. Port rudder sensor
4. Status, A means data is valid
5. Checksum
*/

static void nmea0183Rudder( StringBuffer * msg183, int src, const char * msg )
{
  char positionString[30];

  if (getJSONValue(msg, "Position", positionString, sizeof(positionString)))
  {
    double pos = strtod(positionString, 0);

    nmea0183CreateMessage(msg183, src, "RSA,%04.1f,A,,F", -pos);
  }
}

static bool matchFilter( int n, char * filter )
{
  bool negativeMatch = false;
  int  f;

  while (filter[0])
  {
    if (filter[0] == '!')
    {
      filter++;
      negativeMatch = true;
    }
    f = (int) strtol(filter, &filter, 10);
    logDebug("Src [%ld] == [%ld]?\n", n, f);

    if (n == f)
    {
      logDebug("Src [%ld] matches [%ld]\n", n, f);
      if (negativeMatch)
      {
        return false;
      }
      return true;
    }
    while (filter[0] && filter[0] != ',')
    {
      filter++;
    }
    if (filter[0] == ',')
    {
      filter++;
    }
  }
  return negativeMatch;
}

void convertJSONToNMEA0183( StringBuffer * msg183, const char * msg )
{
  char           str[20];
  int            prn;
  int            src;
  struct timeval tv;
  int            rateType;

  if (!getJSONValue(msg, "pgn", str, sizeof(str)))
  {
    return;
  }
  prn = atoi(str);

  switch (prn)
  {
  case PGN_VESSEL_HEADING:
    rateType = RATE_VESSEL_HEADING;
    break;
  case PGN_WIND_DATA:
    rateType = RATE_WIND_DATA;
    break;
  case PGN_WATER_DEPTH:
    rateType = RATE_WATER_DEPTH;
    break;
  case PGN_WATER_SPEED:
    rateType = RATE_WATER_SPEED;
    break;
  case PGN_ENVIRONMENTAL:
    rateType = RATE_ENVIRONMENTAL;
    break;
  case PGN_DISTANCE_LOG:
    rateType = RATE_DISTANCE_LOG;
    break;
  case PGN_RUDDER:
    rateType = RATE_RUDDER;
    break;
  case PGN_SOG_COG:
    rateType = RATE_GPS_SPEED;
    break;
  case PGN_GPS_DOP:
    rateType = RATE_GPS_DOP;
    break;
  case PGN_POSITION:
    rateType = RATE_GPS_POSITION;
    break;
  case PGN_AIS_A:
  case PGN_AIS_B:
    rateType = RATE_NO_LIMIT;
    break;
  default:
    return;
  }

  if (!getJSONValue(msg, "src", str, sizeof(str)))
  {
    return;
  }
  src = atoi(str);
  if (srcFilter && !matchFilter(src, srcFilter))
  {
    return;
  }

  logDebug("NMEA passed filter for prn %d src %d\n", src, prn);

  if (rateLimit && rateType != RATE_NO_LIMIT)
  {
    int64_t now = epoch();

    if (rateLimitPassed[src][rateType] > (now - 1000L))
    {
      logDebug("Ratelimit for prn %d src %d not reached\n", src, prn);
      return;
    }
    rateLimitPassed[src][rateType] = now;
    logDebug("Ratelimit passed for prn %d src %d\n", src, prn);
  }

  switch (prn)
  {
  case PGN_VESSEL_HEADING:
    nmea0183VesselHeading(msg183, src, msg);
    break;
  case PGN_WIND_DATA:
    nmea0183WindData(msg183, src, msg);
    break;
  case PGN_WATER_DEPTH:
    nmea0183WaterDepth(msg183, src, msg);
    break;
  case PGN_WATER_SPEED:
    nmea0183WaterSpeed(msg183, src, msg);
    break;
  case PGN_ENVIRONMENTAL:
    nmea0183WaterTemperature(msg183, src, msg);
    break;
  case PGN_DISTANCE_LOG:
    nmea0183DistanceTraveled(msg183, src, msg);
    break;
  case PGN_RUDDER:
    nmea0183Rudder(msg183, src, msg);
    break;
  case PGN_SOG_COG:
    nmea0183VTG(msg183, src, msg);
    break;
  case PGN_GPS_DOP:
    nmea0183GSA(msg183, src, msg);
    break;
  case PGN_POSITION:
    nmea0183GLL(msg183, src, msg);
    break;
  case PGN_AIS_A:
  case PGN_AIS_B:
    nmea0183AIVDM(msg183, src, msg);
    break;
  default:
    return;
  }

}

