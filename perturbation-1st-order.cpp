/*
 * perturbation-1st-order.cpp
 *
 * Copyright 2017 Bineet Dash <bineet@bineet-ubuntu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 *
 */

#define EIGEN_USE_LAPACK

#include <iostream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <cmath>
#include <fstream>
#include <chrono>
#include <string>
#include <Eigen/Dense>
#include <lapacke.h>

#include "common_globals.hpp"
#include "configuration.hpp"

using namespace std;
using namespace std::chrono;
using namespace Eigen;

typedef std::complex <double> cd;


VectorXd point(no_of_pts+1);
MatrixXcd states(point.size(),no_of_sps);

double Sqr(cd x){return (x*conj(x)).real();}
double filter(double x) {if(x<1e-8) return 0.0; else return x;}
bool compare(const pair<double, VectorXcd>&i, const pair<double, VectorXcd>&j) {return i.first < j.first;}
void show_time(milliseconds begin_ms, milliseconds end_ms, string s);

bool diagonalize(MatrixXcd Ac, VectorXcd& lambdac, MatrixXcd& vc)
{
  int N;
  if(Ac.cols()==Ac.rows())  N = Ac.cols(); else return false;

  MatrixXd A = Ac.real();
  lambdac.resize(N);
  vc.resize(N,N);
  VectorXd lambda = lambdac.real();

  int LDA = A.outerStride();
  int INFO = 0;
  char Uchar = 'U';
  char Vchar = 'V';
  int LWORK = 5*(2*LDA*LDA+6*LDA+1);
  int LIWORK = 5*(3+5*LDA);

  VectorXd WORK(LWORK);
  VectorXi IWORK(IWORK);

  dsyevd_(&Vchar, &Uchar, &N, A.data(), &LDA, lambda.data(),  WORK.data(), &LWORK, IWORK.data(), &LIWORK, &INFO);
  vc.real() = A;
  lambdac.real() = lambda;
  return INFO==0;
}

double rho_H(double r_prime)
{
  int n_prime = int((r_prime - low_lim)/dx);
  double rho=0.0;
  for(int i=0; i< no_of_sps; i++)
  {
    rho += (conj(states(n_prime,i))*states(n_prime,i)).real();
  }          //phi_i*(r')phi_i(r')
  return rho;
}

double rho_HF(double r, double r_prime)
{
  double num=0.0; double denom=0.0;
  int n = (r - low_lim)/dx;
  int n_prime = (r_prime - low_lim)/dx;

  for(int k=0; k< no_of_sps; k++)
  {
    for(int j=0; j< no_of_sps; j++)
    {
      num += (conj(states(n_prime,k))*states(n,k)*conj(states(n,j))*states(n_prime,j)).real();
    }        //phi_k*(r')phi_k(r)phi_j*(r)phi_j(r')
  }
  denom = rho_H(r);
  if(denom != 0) return num/denom; else return 0.0;
}

double vhf_integrand(double r, double r_prime)
{
  return (rho_H(r_prime)-rho_HF(r,r_prime))/(abs(r - r_prime)+1/(2.0*double(number_of_mesh)));
}

double vhf(double r)
{
  double trapez_sum;
  double fa, fb,x, step;

  step=(up_lim - low_lim)/((double) number_of_mesh);
  fa = vhf_integrand(r,low_lim)/2.0;
  fb = vhf_integrand(r,up_lim)/2.0;
  trapez_sum=0.;
  for (int j=1; j < number_of_mesh; j++)
  {
    x=j*step+low_lim;
    trapez_sum+=vhf_integrand(r,x);
  }
  trapez_sum=(trapez_sum+fb+fa)*step;
  return trapez_sum;
}

double first_order_perturbation(int pos, int i)
{
  return Sqr(states(pos,i))*vhf(point(pos));
}

double integrate_perturbation(int i)
{
  double trapez_sum;
  double fa, fb,x, step;

  fa=first_order_perturbation(0,i)/2.0;
  fb=first_order_perturbation(point.size()-1,i)/2.0;
  trapez_sum=0.;

  for (int j=1; j < point.size()-1; j++)
  {
    trapez_sum+=first_order_perturbation(j,i);
  }
  trapez_sum=(trapez_sum+fb+fa)*dx;
  return trapez_sum;
}

double integrate_psi(int state)
{
  double fa= Sqr(states(0,state))/2.0;
  double fb= Sqr(states(states.rows()-1,state))/2.0;
  double trapez_sum=0.0;
  for (int j=1; j < point.size()-1; j++) trapez_sum+= Sqr(states(j,state));
  return (trapez_sum+fb+fa)*dx;
}

int main()
{
  cout << "Enter separation and no_of_wells: "; cin >> separation >> no_of_wells;

  for(int i=0; i<= no_of_pts; i++) {point(i)=low_lim+i*dx;}

  MatrixXcd H = MatrixXcd::Zero(point.size(),point.size());
  for(int i=0; i<point.size(); i++)
  {
      int j = (i==point.size()-1)? 0 : i+1;
      H(i,j)= -1/(2*dx*dx);
      H(j,i)= -1/(2*dx*dx);
      H(i,i) = 1/(dx*dx)+ V(point(i));
  }

  VectorXcd v; MatrixXcd eigenvectors; VectorXd eigenvalues;
  int master_loop = 1;
  VectorXd oldeival= VectorXd::Zero(no_of_sps);
  VectorXd neweival= VectorXd::Zero(no_of_sps);

  diagonalize(H,v,eigenvectors);   eigenvalues = v.real();
  vector < pair<double,VectorXcd> > eigenspectrum;
  for(int i=0; i<point.size(); i++)
    eigenspectrum.push_back(make_pair(eigenvalues(i),eigenvectors.col(i)));
  sort(eigenspectrum.begin(),eigenspectrum.end(),compare);
  eigenspectrum.resize(no_of_sps);

  for(int i=0; i<no_of_sps; i++) states.col(i)= eigenspectrum[i].second;

  for(int j=0; j<no_of_sps; j++) states.col(j) = states.col(j)/sqrt(integrate_psi(j));

   ofstream fout("data/initialstate.txt");
   for(int i=0; i<point.size(); i++)
   {
     fout << point(i) << " ";
     for(int j=0; j<no_of_sps; j++) fout << Sqr(states(i,j)) << " ";
     fout << endl;
   }
   fout.close();
   cout.precision(8);

   for(int i=0; i< no_of_sps; i++)
    std::cout << "The first_order_perturbation for state:"<< i << " is: " << integrate_perturbation(i) << '\n';
}