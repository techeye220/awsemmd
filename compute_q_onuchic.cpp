/* ----------------------------------------------------------------------
Copyright (2010) Aram Davtyan and Garegin Papoian

Papoian's Group, University of Maryland at Collage Park
http://papoian.chem.umd.edu/


Last Update: 08/18/2011
------------------------------------------------------------------------- */

#include "mpi.h"
#include "math.h"
#include "string.h"
#include "compute_q_onuchic.h"
#include "atom.h"
#include "update.h"
#include "domain.h"
#include "group.h"
#include "error.h"

#include <stdio.h>
#include <stdlib.h>

using namespace LAMMPS_NS;

/* ---------------------------------------------------------------------- */

// Possible use of Compute QOnuchic
// compute 	1 alpha_carbons qonuchic qonuchic.dat cutoff r_cutoff ca_xyz_native.dat tolerance_factor
// compute 	1 alpha_carbons qonuchic qonuchic.dat shadow shadow_map_file tolerance_factor

ComputeQOnuchic::ComputeQOnuchic(LAMMPS *lmp, int narg, char **arg) :
  Compute(lmp, narg, arg)
{
  if (narg != 7 && narg != 8) error->all("Illegal compute qonuchic command");

  int len;
  char *ctype;

  scalar_flag = 1;
  extscalar = 1;
  
  allocated = false;
  
  igroup = group->find(arg[1]);
  groupbit = group->bitmask[igroup];
  if (igroup == -1) 
		error->all("Could not find compute qonuchic group ID"); 
  
  len = strlen(arg[3]) + 1;
  filename = new char[len];
  strcpy(filename,arg[3]);
  
  len = strlen(arg[4]) + 1;
  ctype = new char[len];
  strcpy(ctype,arg[4]);
  
  if (strcmp(ctype, "cutoff")==0) {
  	type = T_CUTOFF;
  	
  	if (narg != 8) error->all("Illegal compute qonuchic command");

  	r_contact = atof(arg[5]);
  	r_consq = r_contact*r_contact;
 	
  	len = strlen(arg[6]) + 1;
  	datafile = new char[len];
  	strcpy(datafile,arg[6]);
  	
 	factor = atof(arg[7]);
  } else if (strcmp(ctype, "shadow")==0) {
  	type = T_SHADOW;
  	
  	if (narg != 7) error->all("Illegal compute qonuchic command");
  	
  	len = strlen(arg[5]) + 1;
  	datafile = new char[len];
  	strcpy(datafile,arg[5]);
  	
 	factor = atof(arg[6]);
  } else {
  	error->all("Wrong type for compute qonuchic"); 
  }
  
  createContactArrays();
  
  fout = fopen(filename, "w");
}

/* ---------------------------------------------------------------------- */

void ComputeQOnuchic::allocate()
{
	int i;
	if (type==T_CUTOFF) x_native = new double*[nAtoms];
	rsq_native = new double*[nAtoms];
	is_native = new bool*[nAtoms];
	for (i=0; i<nAtoms; ++i) {
		if (type==T_CUTOFF) x_native[i] = new double[3];
		rsq_native[i] = new double[nAtoms];
		is_native[i] = new bool[nAtoms];
	}
	
	allocated = true;
}

/* ---------------------------------------------------------------------- */

ComputeQOnuchic::~ComputeQOnuchic()
{
	if (allocated) {
		int i;
		for (i=0;i<nAtoms;++i) {
			if (type==T_CUTOFF) delete [] x_native[i];
			delete [] rsq_native[i];
			delete [] is_native[i];
		}

		if (type==T_CUTOFF) delete [] x_native;
		delete [] rsq_native;
		delete [] is_native;
	}
	
	fclose(fout);
}

/* ---------------------------------------------------------------------- */

void ComputeQOnuchic::init()
{
  if (atom->tag_enable == 0)
    error->all("Cannot use compute qonuchic unless atoms have IDs");
}

/* ---------------------------------------------------------------------- */

void ComputeQOnuchic::createContactArrays()
{
  int i, j;

  if (type==T_CUTOFF) {
	  FILE *fnative;
	  
	  fnative = fopen(datafile, "r");
	  if (!fnative) error->all("Compute qonuchic: can't read native coordinates");
	  fscanf(fnative, "%d",&nAtoms);
	  allocate();
	  for (i=0;i<nAtoms;++i) {
		fscanf(fnative, "%lf %lf %lf",&x_native[i][0], &x_native[i][1], &x_native[i][2]);
	  }
	  fclose(fnative);
	  
	  if (group->count(igroup)!=nAtoms)
		error->all("Compute qonuchic: atom number mismatch");
	  
	  double dx[3];
	  qnorm = 0.0;
	  for (i=0;i<nAtoms;++i) {
		for (j=0;j<nAtoms;++j) {
			dx[0] = x_native[j][0] - x_native[i][0];
			dx[1] = x_native[j][1] - x_native[i][1];
			dx[2] = x_native[j][2] - x_native[i][2];
			
			rsq_native[i][j] = dx[0]*dx[0]+dx[1]*dx[1]+dx[2]*dx[2];
			
			if (rsq_native[i][j] < r_consq && j-i>3) {
				is_native[i][j] = true;
				qnorm += 1.0;
			} else is_native[i][j] = false;
					
			if (is_native[i][j]) rsq_native[i][j] *= factor*factor;
		}
	  }
	  qnorm = 1/qnorm;
  } else if (type==T_SHADOW) {
  	FILE *fshadow;
  	int ires, jres;
  	double rn;
  	
  	nAtoms = group->count(igroup);
  	allocate();
  	
  	for (i=0;i<nAtoms;++i) {
  		for (j=0;j<nAtoms;++j) {
  			is_native[i][j] = false;
  			rsq_native[i][j] = 0.0;
  		}
  	}
  	
  	qnorm = 0.0;  	
  	fshadow = fopen(datafile, "r");
  	if (!fshadow) error->all("Compute qonuchic: can't read contact map file");
  	while (!feof(fshadow)) {
  		fscanf(fshadow, "%d %d %lf\n",&ires, &jres, &rn);
  		ires--;
  		jres--;
  		
  		if (ires>=nAtoms || jres>=nAtoms || ires<0 || jres<0 || abs(jres-ires)<=3)
  			error->all("Compute qonuchic: wrong residue index in shadow contact map file");
  		
//  		fprintf(screen, "|%d %d %f|\n", ires, jres, rn);
  		is_native[ires][jres] = true;
  		rsq_native[ires][jres] = factor*factor*rn*rn;
  		qnorm += 1.0;
  	}
  	fprintf(screen, "qnorm=%f\n", qnorm);
  	qnorm = 1/qnorm;
  	fclose(fshadow);
  }
}

/* ---------------------------------------------------------------------- */

double ComputeQOnuchic::compute_scalar()
{
  invoked_scalar = update->ntimestep;
  
  int i, j, itype, jtype, ires, jres;
  double xtmp,ytmp,ztmp,delx,dely,delz,rsq;
  double q=0.0;

  double **x = atom->x;
  int *mask = atom->mask;
  int *type = atom->type;
  int *tag = atom->tag;
  int *res = atom->residue;
  int nlocal = atom->nlocal;
  int nall = atom->nlocal + atom->nghost;
  
  for (i=0;i<nlocal;i++) {
    if (!(mask[i] & groupbit)) continue;
    
  	itype = type[i];
    ires = res[i]-1;
    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    
    for (j=i+1;j<nall;j++) {
      if (!(mask[j] & groupbit)) continue;
      
      jtype = type[j];
      jres = res[j]-1;
      
      if (is_native[ires][jres] && abs(jres-ires)>3) {
        delx = xtmp - x[j][0];
		dely = ytmp - x[j][1];
		delz = ztmp - x[j][2];
		rsq = delx*delx + dely*dely + delz*delz;
		  
		if (rsq<rsq_native[ires][jres]) q += 1.0;
      }
    }
  }
  
  MPI_Allreduce(&q,&scalar,1,MPI_DOUBLE,MPI_SUM,world);
  scalar *= qnorm;
  
  fprintf(fout, "%d %f\n", invoked_scalar, scalar);
  
  return scalar;
}