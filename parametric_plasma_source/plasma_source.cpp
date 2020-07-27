#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include "plasma_source.hpp"
#include <stdlib.h>     
#include "openmc/random_lcg.h"
#include "xml_helper.hpp"

#define RANDOM openmc::prn()

namespace plasma_source {

// default constructor
PlasmaSource::PlasmaSource() {}

// large constructor
PlasmaSource::PlasmaSource(const double ion_density_ped, const double ion_density_sep,
	    const double ion_density_origin, const double ion_temp_ped,
	    const double ion_temp_sep, const double ion_temp_origin, 
	    const double pedistal_rad, const double ion_density_peak,
	    const double ion_temp_peak, const double ion_temp_beta,
      const double minor_radius, const double major_radius,
      const double elongation, const double triangularity,
      const double shafranov, const std::string plasma_type,
      const int plasma_id, const int number_of_bins,
      const double min_toroidal_angle,
      const double max_toroidal_angle ) {

  // set params
  ionDensityPedistal = ion_density_ped;
  ionDensitySeperatrix = ion_density_sep;
  ionDensityOrigin = ion_density_origin;
  ionTemperaturePedistal = ion_temp_ped;
  ionTemperatureSeperatrix = ion_temp_sep;
  ionTemperatureOrigin = ion_temp_origin;
  pedistalRadius = pedistal_rad;
  ionDensityPeaking = ion_density_peak;
  ionTemperaturePeaking = ion_temp_peak;
  ionTemperatureBeta = ion_temp_beta;
  minorRadius = minor_radius;
  majorRadius = major_radius;
  this->elongation = elongation;
  this->triangularity = triangularity;
  this->shafranov = shafranov;
  plasmaType = plasma_type;
  plasmaId = plasma_id;
  numberOfBins = number_of_bins;
  minToroidalAngle = min_toroidal_angle/180.*M_PI;
  maxToroidalAngle = max_toroidal_angle/180.*M_PI;

  setup_plasma_source();
}

// destructor
PlasmaSource::~PlasmaSource(){}

// main master sample function
void PlasmaSource::SampleSource(std::array<double,8> random_numbers,
                                double &x,
                                double &y,
                                double &z,
                                double &u,
                                double &v,
                                double &w,
                                double &E) {
  double radius = 0.;
  int bin = 0;
  sample_source_radial(random_numbers[0],random_numbers[1],radius,bin);
  double r = 0.;
  convert_rad_to_rz(radius,random_numbers[2],r,z);
  convert_r_to_xy(r,random_numbers[3],x,y);
  sample_energy(bin,random_numbers[4],random_numbers[5],E);
  isotropic_direction(random_numbers[6],random_numbers[7],
                      u,v,w);

}

/*
 * sample the pdf src_profile, to generate the sampled minor radius
 */
void PlasmaSource::sample_source_radial(double rn_store1, double rn_store2, 
                          double &sampled_radius, int &sampled_bin) {
  
  for ( int i = 0 ; i < numberOfBins ; i++ ) {
    if ( rn_store1 <= source_profile[i] ) {
      if ( i > 0 ) {
	      sampled_radius = (float(i-1)*(binWidth)) + (binWidth*(rn_store2));
	      sampled_bin = i;
	      return;
      } else {
	      sampled_radius = binWidth*(rn_store2);
	      sampled_bin = i;
	      return;
      }
    }
  }

  std::cerr << "error" << std::endl;
  std::cerr << "Sample position greater than plasma radius" << std::endl;
  exit(1);
  return;
}

/*
 * sample the energy of the neutrons, updates energy neutron in mev
 */
void PlasmaSource::sample_energy(const int bin_number, double random_number1, double random_number2,
		      double &energy_neutron) {
  // generate the normally distributed number
  const double twopi = 6.28318530718;
  double sample1 = std::sqrt(-2.0*std::log(random_number1));
  double sample2 = cos(twopi*(random_number2));
  energy_neutron = (5.59/2.35)*(ion_kt[bin_number])*sample1*sample2;
  energy_neutron += 14.08;
  // test energy limit
  // if (energy_neutron < 15.5){energy_neutron = 15.5} else {}
  return;
}

/*
 * convert the sampled radius to an rz coordinate by using plasma parameters
 */
void PlasmaSource::convert_rad_to_rz( const double minor_sampled,
			                  const double rn_store, 
                        double &radius, double &height)
{
  const double twopi = 6.28318530718;
  
  double alpha = twopi*(rn_store);
  
  double shift = shafranov*(1.0-std::pow(minor_sampled/(minorRadius),2));
  
  radius = majorRadius + minor_sampled*cos(alpha+(triangularity*sin(alpha))) + shift;
  height = elongation*minor_sampled*sin(alpha);
  
 
  return;
}
  

/*
 * convert rz_to_xyz
 */
void PlasmaSource::convert_r_to_xy(const double r, const double rn_store, 
                     double &x, double &y) 
                    
{
  double toroidal_extent = maxToroidalAngle - minToroidalAngle;
  double toroidal_angle = toroidal_extent*rn_store + minToroidalAngle;
  x = r*sin(toroidal_angle);
  y = r*cos(toroidal_angle);
  return;
}

/*
 * sets up the cumulatitive probability profile
 * on the basis of the ion temp and ion density
 * this portion is deterministic
 */
void PlasmaSource::setup_plasma_source()
{
  double ion_d; // ion density
  double ion_t; // ion temp

  std::vector<double> src_strength; // the source strength, n/m3
  double r;

  binWidth = minorRadius/float(numberOfBins);
  double total = 0.0; // total source strength

  for (int i = 0 ; i < numberOfBins ; i++) {
    r = binWidth * float(i);
    ion_d = ion_density(r);
    ion_t = ion_temperature(r);
    src_strength.push_back(std::pow(ion_d,2)*dt_xs(ion_t));
    ion_kt.push_back(sqrt(ion_t/1000.0)); // convert to sqrt(MeV)
    total += src_strength[i];
  }

  // normalise the source profile
  double sum = 0 ;
  for ( int i = 0 ; i < numberOfBins ; i++) {
	sum += src_strength[i];
    source_profile.push_back(sum/total);
  }
  return;
}

/*
 * function that returns the ion density given the 
 * given the critical plasma parameters
 */
double PlasmaSource::ion_density(const double sample_radius)
{
  double ion_dens = 0.0;

  if( plasmaId == 0 ) {
    ion_dens = ionDensityOrigin*
      (1.0-std::pow(sample_radius/minorRadius,2));
  } else {
    if(sample_radius <= pedistalRadius) {
      ion_dens += ionDensityPedistal;
      double product;
      product = 1.0-std::pow(sample_radius/pedistalRadius,2);
      product = std::pow(product,ionDensityPeaking);
      ion_dens += (ionDensityOrigin-ionDensityPedistal)*
            	  (product);
    } else {
      ion_dens += ionDensitySeperatrix;
      double product;
      product = ionDensityPedistal - ionDensitySeperatrix;
      ion_dens += product*(minorRadius-sample_radius)/(minorRadius-pedistalRadius);
    }
  }

  return ion_dens;
}
		   
/*
 * function that returns the ion density given the 
 * given the critical plasma parameters
 */
double PlasmaSource::ion_temperature(const double sample_radius)
{
  double ion_temp = 0.0;

  if( plasmaId == 0 ) {
    ion_temp = ionTemperatureOrigin*
      (1.0-std::pow(sample_radius/minorRadius,
		    ionTemperaturePeaking));
  } else {
    if(sample_radius <= pedistalRadius) {
      ion_temp += ionTemperaturePedistal;
      double product;
      product = 1.0-std::pow(sample_radius/pedistalRadius,ionTemperatureBeta);
      product = std::pow(product,ionTemperaturePeaking);
      ion_temp += (ionTemperatureOrigin-
		   ionTemperaturePedistal)*(product);
    } else {
      ion_temp += ionTemperatureSeperatrix;
      double product;
      product = ionTemperaturePedistal - ionTemperatureSeperatrix;
      ion_temp += product*(minorRadius-sample_radius)/(minorRadius-pedistalRadius);
    }
  }

  return ion_temp;
}

/*
 * returns the dt cross section for a given ion temp
 */
double PlasmaSource::dt_xs(double ion_temp)
{
  double dt;
  double c[7]={2.5663271e-18,19.983026,2.5077133e-2,
	       2.5773408e-3,6.1880463e-5,6.6024089e-2,
	       8.1215505e-3};
  
  double u = 1.0-ion_temp*(c[2]+ion_temp*(c[3]-c[4]*ion_temp))
    /(1.0+ion_temp*(c[5]+c[6]*ion_temp));

  dt = c[0]/(std::pow(u,5./6.)*std::pow(ion_temp,2./3.0));
  dt *= exp(-1.*c[1]*std::pow(u/ion_temp,1./3.));

  return dt;
}

/*
 * returns the dt cross section for a given ion temp
 */
void PlasmaSource::isotropic_direction(const double random1, 
                                         const double random2,
                                         double &u, double &v,
                                         double &w) {
  double t = 2*M_PI*random1;
  double p = acos(1. - 2.*random2);

  u = sin(p)*cos(t);
  v = sin(p)*sin(t);
  w = cos(p);
  
  return;
}

std::string PlasmaSource::to_xml() {
  XMLHelper xml_helper = XMLHelper("PlasmaSource");
  xml_helper.add_element("MajorRadius", majorRadius);
  xml_helper.add_element("MinorRadius", minorRadius);
  xml_helper.add_element("Elongation", elongation);
  xml_helper.add_element("Triangularity", triangularity);
  xml_helper.add_element("ShafranovShift", shafranov);
  xml_helper.add_element("PedestalRadius", pedistalRadius);
  xml_helper.add_element("IonDensityPedestal", ionDensityPedistal);
  xml_helper.add_element("IonDensitySeparatrix", ionDensitySeperatrix);
  xml_helper.add_element("IonDensityOrigin", ionDensityOrigin);
  xml_helper.add_element("IonTemperaturePedestal", ionTemperaturePedistal);
  xml_helper.add_element("IonTemperatureSeparatrix", ionTemperatureSeperatrix);
  xml_helper.add_element("IonTemperatureOrigin", ionTemperatureOrigin);
  xml_helper.add_element("IonDensityAlpha", ionDensityPeaking);
  xml_helper.add_element("IonTemperatureAlpha", ionTemperaturePeaking);
  xml_helper.add_element("IonTemperatureBeta", ionTemperatureBeta);
  xml_helper.add_element("PlasmaType", plasmaType);
  xml_helper.add_element("PlasmaId", plasmaId);
  xml_helper.add_element("NumberOfBins", numberOfBins);
  xml_helper.add_element("MinimumToroidalAngle", minToroidalAngle);
  xml_helper.add_element("MaximumToroidalAngle", maxToroidalAngle);
  return xml_helper.to_string();
}

bool PlasmaSource::to_xml(std::string output_path) {
  bool success = true;
  std::string output = to_xml();
  std::ofstream out;
  out.open(output_path);
  if (out.is_open()) {
    out << output;
    out.close();
  }
  else {
    success = false;
  }
  return success;
}

PlasmaSource PlasmaSource::from_file(std::string input_path) {
  pugi::xml_document doc;
  pugi::xml_parse_result result = doc.load_file(input_path.c_str());
  if (!result) {
    throw std::runtime_error("Error reading plasma source from file " + input_path + ". Error = " + result.description());
  }

  pugi::xml_node root_node = doc.root().child("PlasmaSource");

  double major_radius = root_node.child("MajorRadius").text().as_double();
  double minor_radius = root_node.child("MinorRadius").text().as_double();
  double elongation = root_node.child("Elongation").text().as_double();
  double triangularity = root_node.child("Triangularity").text().as_double();
  double shafranov_shift = root_node.child("ShafranovShift").text().as_double();
  double pedestal_radius = root_node.child("PedestalRadius").text().as_double();
  double ion_density_pedestal = root_node.child("IonDensityPedestal").text().as_double();
  double ion_density_separatrix = root_node.child("IonDensitySeparatrix").text().as_double();
  double ion_density_origin = root_node.child("IonDensityOrigin").text().as_double();
  double ion_temperature_pedestal = root_node.child("IonTemperaturePedestal").text().as_double();
  double ion_temperature_separatrix = root_node.child("IonTemperatureSeparatrix").text().as_double();
  double ion_temperature_origin = root_node.child("IonTemperatureOrigin").text().as_double();
  double ion_density_alpha = root_node.child("IonDensityAlpha").text().as_double();
  double ion_temperature_alpha = root_node.child("IonTemperatureAlpha").text().as_double();
  double ion_temperature_beta = root_node.child("IonTemperatureBeta").text().as_double();
  std::string plasma_type = root_node.child("PlasmaType").text().as_string();
  int plasma_id = root_node.child("PlasmaId").text().as_int();
  int number_of_bins = root_node.child("NumberOfBins").text().as_int();
  double min_toroidal_angle = root_node.child("MinimumToroidalAngle").text().as_double();
  double max_toroidal_angle = root_node.child("MaximumToroidalAngle").text().as_double();

  PlasmaSource plasma_source = PlasmaSource(
    ion_density_pedestal, ion_density_separatrix, ion_density_origin, ion_temperature_pedestal, ion_temperature_separatrix,
    ion_temperature_origin, pedestal_radius, ion_density_alpha, ion_temperature_alpha, ion_temperature_beta, minor_radius,
    major_radius, elongation, triangularity, shafranov_shift, plasma_type, plasma_id, number_of_bins, min_toroidal_angle,
    max_toroidal_angle);
  return plasma_source;
}

} // end of namespace
