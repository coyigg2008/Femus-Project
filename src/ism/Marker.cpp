/*=========================================================================

 Program: FEMUS
 Module: Mesh
 Authors: Eugenio Aulisa, Giacomo Capodaglio

 Copyright (c) FEMTTU
 All rights reserved.

 This software is distributed WITHOUT ANY WARRANTY; without even
 the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

//----------------------------------------------------------------------------
// includes :
//----------------------------------------------------------------------------
#include "Marker.hpp"
#include "NumericVector.hpp"

const unsigned facePointNumber[6] = {0, 0, 0, 9, 7, 0};


const unsigned facePoints[6][9] = {
  { },
  { },
  { },
  { 0, 4, 1, 5, 2, 6, 3, 7, 0},
  { 0, 3, 1, 4, 2, 5, 0},
  { }
};

namespace femus {

  void Marker::GetElement() {

    unsigned dim = _mesh->GetDimension();
    std::vector < unsigned > processorMarkerFlag(_nprocs, 3);
    for(unsigned j = 0 ; j < _nprocs; j++) {
      std::cout << " processorMarkerFlag[" << j << "] = " << processorMarkerFlag[j] << std::endl;
    }

    double modulus = 1.e10;
    int iel = _mesh->_elementOffset[_iproc + 1];

    for(int kel = _mesh->_elementOffset[_iproc]; kel < _mesh->_elementOffset[_iproc + 1]; kel += 25) {
      short unsigned kelType = _mesh->GetElementType(kel);
      double modulusKel = 0.;
      for(unsigned i = 0; i < facePointNumber[kelType] - 1; i++) {
        unsigned kDof  = _mesh->GetSolutionDof(facePoints[kelType][i], kel, 2);    // global to global mapping between coordinates node and coordinate dof
        double distance2 = 0;
        for(unsigned k = 0; k < dim; k++) {
          double dk = (*_mesh->_topology->_Sol[k])(kDof) - _x[k];     // global extraction and local storage for the element coordinates
          distance2 += dk * dk;
        }
        modulusKel += sqrt(distance2);
      }
      modulusKel /= (facePointNumber[kelType] - 1);

      if(modulusKel < modulus) {
        iel = kel;
        modulus = modulusKel;
      }
    }

    if(iel == _mesh->_elementOffset[_iproc + 1]) {
      std::cout << "Warning the marker is located on unreasonable distance from the mesh >= 1.e10" << std::endl;
    }
    else {
      std::cout << "the smart search starts from element " << iel << std::endl;
    }


    bool elementHasBeenFound = false;
    bool pointIsOutsideThisMesh = false;
    bool pointIsOutsideTheDomain = false;
    unsigned nextProc = _iproc;
    int kel = iel;
    int jel;

    while(!elementHasBeenFound) {

      while(elementHasBeenFound + pointIsOutsideThisMesh + pointIsOutsideTheDomain == 0) {
        jel = GetNextElement(dim, iel, kel);
        kel = iel;

        if(jel == iel) {
          _elem = iel;
          elementHasBeenFound = true;
          processorMarkerFlag[_iproc] = 1;
        }
        else if(jel < 0) {
          pointIsOutsideTheDomain = true;
          processorMarkerFlag[_iproc] = 0;
        }
        else {
          nextProc = _mesh->IsdomBisectionSearch(jel, 3);
          if(nextProc != _iproc) {
            pointIsOutsideThisMesh = true;
            processorMarkerFlag[_iproc] = 2;
          }
          else {
            iel = jel;
          }
        }
      }

      std::cout << std::flush;
      MPI_Barrier(PETSC_COMM_WORLD);

      if(elementHasBeenFound) {
        std::cout << " The marker belongs to element " << _elem << std::endl;
      }
      else if(pointIsOutsideTheDomain) {
        std::cout << " The marker does not belong to this domain" << std::endl;
      }
      else if(pointIsOutsideThisMesh) {
        std::cout << "proc " << _iproc << " beleives the marker is in proc = " << nextProc << std::endl;
      }


      for(unsigned jproc = 0; jproc < _nprocs; jproc++) {
        if(jproc != _iproc) {
          if(processorMarkerFlag[_iproc] == 2 && jproc == nextProc) {
            unsigned three = 3;
            MPI_Send(&three, 1, MPI_UNSIGNED, jproc, 1 , PETSC_COMM_WORLD);
          }
          else {
            MPI_Send(&processorMarkerFlag[_iproc], 1, MPI_UNSIGNED, jproc, 1 , PETSC_COMM_WORLD);
          }
          MPI_Recv(&processorMarkerFlag[jproc], 1, MPI_UNSIGNED, jproc, 1 , PETSC_COMM_WORLD, MPI_STATUS_IGNORE);
        }
      }

      elementHasBeenFound = false;
      for(unsigned i = 0; i < _nprocs; i++) {
        if(processorMarkerFlag[i] == 1) {
          elementHasBeenFound = true;
          break;
        }
      }


      std::vector< int > nextElem(_nprocs, -1);
      if(!elementHasBeenFound) {
        if(processorMarkerFlag[_iproc] == 2) {
          MPI_Send(&jel, 1, MPI_INT, nextProc, 1 , PETSC_COMM_WORLD);
        }
        for(unsigned jproc = 0; jproc < _nprocs; jproc++) {
          if(processorMarkerFlag[jproc] == 3) {
            MPI_Recv(&nextElem[jproc], 1, MPI_INT, jproc, 1 , PETSC_COMM_WORLD, MPI_STATUS_IGNORE);
          }
        }

        modulus = 1.0e10;
        iel = _mesh->_elementOffset[_iproc + 1];

        for(unsigned jproc = 0; jproc < _nprocs; jproc++) {
          if(processorMarkerFlag[jproc] == 3) {
            unsigned kel = nextElem[jproc];
            short unsigned kelType = _mesh->GetElementType(kel);
            double modulusKel = 0.;
            for(unsigned i = 0; i < facePointNumber[kelType] - 1; i++) {
              unsigned kDof  = _mesh->GetSolutionDof(facePoints[kelType][i], kel, 2);    // global to global mapping between coordinates node and coordinate dof
              double distance2 = 0;
              for(unsigned k = 0; k < dim; k++) {
                double dk = (*_mesh->_topology->_Sol[k])(kDof) - _x[k];     // global extraction and local storage for the element coordinates
                distance2 += dk * dk;
              }
              modulusKel += sqrt(distance2);
            }
            modulusKel /= (facePointNumber[kelType] - 1);

            if(modulusKel < modulus) {
              iel = kel;
              modulus = modulusKel;
            }
          }
        }
        if(iel != _mesh->_elementOffset[_iproc + 1]) {
          std::cout << "start element= " << iel << std::endl;
          pointIsOutsideThisMesh = false;
          nextProc = _iproc;
          kel = iel;
        }
      }


      for(unsigned j = 0 ; j < _nprocs; j++) {
        std::cout << " processorMarkerFlag[" << j << "] = " << processorMarkerFlag[j] << " " << nextElem[j] << std::endl;
      }

    }

  }

// HOW DO WE DEAL WITH SITUATIONS WHERE THE POINT IS ON AN EDGE THAT IS SHARED BY TWO ELEMENTS IN 2 DIFFERENT PROCESSORS?

  int Marker::GetNextElement(const unsigned &dim, const int &currentElem, const int &previousElem) {

    std::vector< std::vector < double > > xv(dim);

    for(unsigned k = 0; k < dim; k++) {
      xv[k].reserve(9);
    }
    short unsigned ielType = _mesh->GetElementType(currentElem);
    for(unsigned k = 0; k < dim; k++) {
      xv[k].resize(facePointNumber[ielType]);
    }
    for(unsigned i = 0; i < facePointNumber[ielType]; i++) {
      unsigned ielDof  = _mesh->GetSolutionDof(facePoints[ielType][i], currentElem, 2);    // global to global mapping between coordinates node and coordinate dof
      for(unsigned k = 0; k < dim; k++) {
        xv[k][i] = (*_mesh->_topology->_Sol[k])(ielDof) - _x[k];     // global extraction and local storage for the element coordinates
      }
    }

    double length = 0.;
    for(unsigned i = 0; i < xv[0].size() - 1; i++) {
      length += sqrt((xv[0][i + 1] - xv[0][i]) * (xv[0][i + 1] - xv[0][i]) +
                     (xv[1][i + 1] - xv[1][i]) * (xv[1][i + 1] - xv[1][i]));
    }

    length /= xv[0].size();

    for(unsigned i = 0; i < xv[0].size(); i++) {
      xv[0][i] /= length;
      xv[1][i] /= length;
    }

    double epsilon  = 10e-10;

    double w = 0.;
    for(unsigned i = 0; i < xv[0].size() - 1; i++) {
      double Delta = -xv[0][i] * (xv[1][i + 1] - xv[1][i]) + xv[1][i] * (xv[0][i + 1] - xv[0][i]);
//       if(currentElem == 125) {
//         std::cout << "Delta for element" << currentElem << " is =" << Delta  << " , " << xv[0][i] << " , " << xv[1][i] << " , " << xv[0][i + 1] << " , " << xv[1][i + 1] << std::endl;
//       }

      //std::cout << "Delta=" << Delta << " " << epsilon << std::endl;

      if(fabs(Delta) > epsilon) {   // the edge does not pass for the origin
        //std::cout << " xv[1][i]*xv[1][i+1] = " << xv[1][i]*xv[1][i + 1] << std::endl;
        if(fabs(xv[1][i]) < epsilon && xv[0][i] > 0) {  // the first vertex is on the positive x-axis
          //std::cout << "the first vertex is on the positive x-axis" << std::endl;
          if(xv[1][i + 1] > 0) w += .5;
          else w -= .5;
        }
        else if(fabs(xv[1][i + 1]) < epsilon && xv[0][i + 1] > 0) { // the second vertex is on the positive x-axis
          //std::cout << "the second vertex is on the positive x-axis" << std::endl;
          if(xv[1][i] < 0) w += .5;
          else w -= .5;
        }
        else if(xv[1][i]*xv[1][i + 1] < 0) { // the edge crosses the x-axis but doesn't pass through the origin
          double r = xv[0][i] - xv[1][i] * (xv[0][i + 1] - xv[0][i]) / (xv[1][i + 1] - xv[1][i]);
          //std::cout << " r = " << r << std::endl;
          if(r > 0) {
            if(xv[1][i] < 0) w += 1.;
            else w -= 1;
          }
        }
      }
      else { // the line trought the edge passes for the origin
        //std::cout << " xv[0][i]*xv[0][i+1] = " << xv[0][i]*xv[0][i + 1] << std::endl;
        if(fabs(xv[0][i]) < epsilon  && fabs(xv[1][i]) < epsilon) {  // vertex 1 is the origin
          w = 1; // set to 1 by default
          //std::cout << "w set to 1 by default (vertex 1 is in the origin)" << std::endl;
          break;
        }
        else if(fabs(xv[0][i + 1]) < epsilon && fabs(xv[1][i + 1]) < epsilon) { // vertex 2 is the origin
          w = 1; // set to 1 by default
          //std::cout << "w set to 1 by default (vertex 2 is in the origin)" << std::endl;
          break;
        }
        else if(xv[0][i] * xv[0][i + 1] < 0 || xv[1][i] * xv[1][i + 1] < 0) { //the edge crosses the origin
          w = 1; // set to 1 by default
          //std::cout << "w set to 1 by default (the edge passes through the origin)" << std::endl;
          break;
        }
      }
      //std::cout << " w = " << w << " and currentElem = " << currentElem << std::endl;
    }

    int nextElem = currentElem;

    if(w == 0) {

      double distance = 1.e10;
      for(unsigned j = 1; j < xv[0].size() - 1; j += 2) {
        double distancej = 0.;
        for(unsigned k = 0; k < dim; k++) {
          distancej += xv[k][j] * xv[k][j];
        }
        distancej = sqrt(distancej);

        if(distancej < distance) {
          int jel = (_mesh->el->GetFaceElementIndex(currentElem, (j - 1) / 2) - 1);
          if(jel != previousElem) {
            nextElem = jel;
            distance = distancej;
          }
        }
      }

    }
    else if(w < 0) {
      std::cout << " Error negative Winding Number with counterclockwise oriented points " << std::endl;
      abort();
    }

    std::cout << "the next element is " << nextElem << std::endl;

    return nextElem;
  }

}
