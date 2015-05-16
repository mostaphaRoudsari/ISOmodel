#include "SolarRadiation.hpp"

namespace openstudio {
namespace isomodel {
/**
 * Surface Azimuths of the "building" to calculate solar radiation for
 */
double SurfaceAzimuths[] =
{ 0, -45, -90, -135, 180, 135, 90, 45 };

SolarRadiation::SolarRadiation(TimeFrame* frame, EpwData* wdata, double tilt)
{
  m_monthlyDryBulbTemp.resize(MONTHS);
  m_monthlyDewPointTemp.resize(MONTHS);
  m_monthlyRelativeHumidity.resize(MONTHS);
  m_monthlyWindspeed.resize(MONTHS);
  m_monthlyGlobalHorizontalRadiation.resize(MONTHS);
  m_monthlySolarRadiation.resize(MONTHS);
  m_hourlyDryBulbTemp.resize(MONTHS);
  m_hourlyDewPointTemp.resize(MONTHS);
  m_hourlyGlobalHorizontalRadiation.resize(MONTHS);
  m_monthlySolarRadiation.resize(MONTHS);
  m_eglobe.resize(TIMESLICES);
  for (int i = 0; i < MONTHS; i++) {
    m_hourlyDryBulbTemp[i].resize(HOURS);
    m_hourlyDewPointTemp[i].resize(HOURS);
    m_hourlyGlobalHorizontalRadiation[i].resize(HOURS);
  }
  for (int i = 0; i < MONTHS; i++) {
    m_monthlySolarRadiation[i].resize(NUM_SURFACES);
  }
  for (int i = 0; i < TIMESLICES; i++) {
    m_eglobe[i].resize(NUM_SURFACES);
  }
  this->frame = frame;
  this->weatherData = wdata;
  this->m_longitude = wdata->longitude() * PI / 180.0; // Convert to radians.
  this->m_localMeridian = wdata->timezone() * 15.0 * PI / 180.0; //compute the local meridian from the time zone.  Negative is W of the prime meridian. Convert to radians.
  this->m_latitude = wdata->latitude() * PI / 180.0; //convert latitute to radians
  this->m_surfaceTilt = tilt / 2.0;	//surface tilt in radians (pi/2 is vertical, 0 is horizontal);
}
SolarRadiation::~SolarRadiation(void)
{
}

/**
 * compute the monthly average solar radiation incident on the vertical surfaces for the 
 * eight primary directions (N, S, E, W, NW, SW, NE, SE)
 * these computations come from ASHRAE2007  Fundamentals , chapter 14
 * or Duffie and Beckman "Solar engineering of thermal processes, 3rd ed",
 * Wiley 2006
 */
void SolarRadiation::calculateSurfaceSolarRadiation()
{
  double rhog = 0.14;	//ground reflectivity coefficient
  double GroundReflected = 0, SolarAzimuthSin = 0, SolarAzimuthCos = 0, SolarAzimuth = 0, Revolution, EquationOfTime, ApparentSolarTime,
      SolarDeclination, SolarHourAngles, SolarAltitudeAngles;

  double AngleOfIncidence, SurfaceSolarAzimuth, DirectBeam, DiffuseRadiation, DiffuseComponent;

  //avoid calling data() to reduce copy time
  std::vector<std::vector<double> > data = weatherData->data();
  std::vector<double> vecEB = data[EB];
  std::vector<double> vecED = data[ED];
  std::vector<double>* vecEGI;
  for (int i = 0; i < TIMESLICES; i++) {
    // First compute the solar azimuth for each hour of the year for our location
    Revolution = calculateRevolutionAngle(frame->YTD[i]);
    EquationOfTime = calculateEquationOfTime(Revolution);
    ApparentSolarTime = calculateApparentSolarTime(frame->Hour[i], EquationOfTime);

    SolarDeclination = calculateSolarDeclination(Revolution);
    SolarHourAngles = calculateSolarHourAngle(ApparentSolarTime);
    SolarAltitudeAngles = calculateSolarAltitudeAngle(SolarDeclination, SolarHourAngles);

    SolarAzimuthSin = calculateSolarAzimuthSin(SolarDeclination, SolarHourAngles, SolarAltitudeAngles);
    SolarAzimuthCos = calculateSolarAzimuthCos(SolarDeclination, SolarHourAngles, SolarAltitudeAngles);
    SolarAzimuth = calculateSolarAzimuth(SolarAzimuthSin, SolarAzimuthCos);

    GroundReflected = (vecEB[i] * sin(SolarAltitudeAngles) + vecED[i]) * rhog * (1 - cos(m_surfaceTilt)) / 2;  // ground reflected component
    vecEGI = &(m_eglobe[i]);

    //then compute the hourly radiation on each vertical surface given the solar azimuth for each hour
    for (int s = 0; s < NUM_SURFACES; s++) {
      SurfaceSolarAzimuth = fabs(SolarAzimuth - (SurfaceAzimuths[s] * (PI / 180.0))); //surface - solar azimuth in degrees, >pi/2 means surface is in shade

      AngleOfIncidence = acos(
          cos(SolarAltitudeAngles) * cos(SurfaceSolarAzimuth) * sin(m_surfaceTilt) + sin(SolarAltitudeAngles) * cos(m_surfaceTilt)); //ancle of incidence of sun's rays on surface in rad

      DirectBeam = vecEB[i] * std::max(cos(AngleOfIncidence), 0.0); //Beam component of radiation

      DiffuseRadiation = std::max(0.45, 0.55 + 0.437 * cos(AngleOfIncidence) + 0.313 * pow(cos(AngleOfIncidence), 2.0)); //Diffuse component of radiation 
      //diffuse component for sigma> pi/2 meaning it is a wall tilted outward, for sigma<= pi/2 meaning wall vertical or tilted inward
      DiffuseComponent =
          (m_surfaceTilt > PI / 2) ?
              vecED[i] * DiffuseRadiation * sin(m_surfaceTilt) : vecED[i] * (DiffuseRadiation * sin(m_surfaceTilt) + cos(m_surfaceTilt));

      (*vecEGI)[s] = DirectBeam + DiffuseComponent + GroundReflected; // add up all the components
    }
  }
}

//average the data in the bins over the count or days
void SolarRadiation::calculateMonthAvg(int midx, int cnt)
{
  int days = 0;
  if (midx > -1) {
    //average rate by month
    m_monthlyDryBulbTemp[midx] /= cnt;
    m_monthlyDewPointTemp[midx] /= cnt;
    m_monthlyRelativeHumidity[midx] /= cnt;
    m_monthlyWindspeed[midx] /= cnt;
    m_monthlyGlobalHorizontalRadiation[midx] /= cnt;
    for (int s = 0; s < NUM_SURFACES; s++) {
      m_monthlySolarRadiation[midx][s] /= cnt;
    }
    //hours are averaged over days in the month
    days = frame->monthLength(midx + 1);
    for (int h = 0; h < 24; h++) {
      m_hourlyDryBulbTemp[midx][h] /= days;
      m_hourlyDewPointTemp[midx][h] /= days;
      m_hourlyGlobalHorizontalRadiation[midx][h] /= days;
    }
  }
}
//Empty bins to start accumulating data for monthly averages
void SolarRadiation::clearMonthlyAvg(int midx)
{
  for (int h = 0; h < 24; h++) {
    m_hourlyDryBulbTemp[midx][h] = 0;
    m_hourlyDewPointTemp[midx][h] = 0;
    m_hourlyGlobalHorizontalRadiation[midx][h] = 0;
  }
  for (int s = 0; s < NUM_SURFACES; s++) {
    m_monthlySolarRadiation[midx][s] = 0;
  }
  m_monthlyDryBulbTemp[midx] = 0;
  m_monthlyDewPointTemp[midx] = 0;
  m_monthlyRelativeHumidity[midx] = 0;
  m_monthlyWindspeed[midx] = 0;
  m_monthlyGlobalHorizontalRadiation[midx] = 0;
}

void SolarRadiation::calculateAverages()
{
  int month = 0;
  int midx = -1;
  int cnt = 0;
  int h = 0;

  std::vector<std::vector<double> > data = weatherData->data();
  std::vector<double> vecDBT = data[DBT];
  std::vector<double> vecDPT = data[DPT];
  std::vector<double> vecRH = data[RH];

  std::vector<double> vecEGH = data[EGH];
  std::vector<double> vecWSPD = data[WSPD];

  for (int i = 0; i < TIMESLICES; i++, cnt++) {
    if (frame->Month[i] != month) {
      month = frame->Month[i];
      //on month change, average bin out over count
      calculateMonthAvg(midx, cnt);
      midx++;
      //reset accumulators
      clearMonthlyAvg(midx);
      cnt = 0;
    }
    //accumulate data into bins
    m_monthlyDryBulbTemp[midx] += vecDBT[i];
    m_monthlyDewPointTemp[midx] += vecDPT[i];
    m_monthlyRelativeHumidity[midx] += vecRH[i];
    m_monthlyGlobalHorizontalRadiation[midx] += vecEGH[i];
    m_monthlyWindspeed[midx] += vecWSPD[i];
    for (int s = 0; s < NUM_SURFACES; s++)
      m_monthlySolarRadiation[midx][s] += m_eglobe[i][s];
    h = frame->Hour[i];
    m_hourlyDryBulbTemp[midx][h] += vecDBT[i];
    m_hourlyDewPointTemp[midx][h] += vecDPT[i];
    m_hourlyGlobalHorizontalRadiation[midx][h] += vecEGH[i];
  }
  //final month average
  calculateMonthAvg(midx, cnt);
}

//Calculate hourly solar radiation for each surface
//and then calculate the monthly/hourly averages
void SolarRadiation::Calculate()
{
  calculateSurfaceSolarRadiation();
  calculateAverages();
}

}
}
