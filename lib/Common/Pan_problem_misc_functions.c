				
#include "element_definitions.h"
#include "global_defs.h"

#include <stdio.h>

#include <math.h>
#include <malloc.h>
#include <sys/types.h>
#include <unistd.h>

#if (defined __sunos__)
#include <string.h>
#else
#include <strings.h>
#endif

#if defined(__sgi) || defined(__osf__)
#include <sys/types.h>
#endif

/*#include "/home/limbo1/louis/Software/src/dmalloc-3.2.1/dmalloc.h"
*/
int get_process_identifier()   
{
    int pid;

    pid = (int) getpid();
    return(pid);
}

void unique_copy_file(E,name,comment)
    struct All_variables *E;
    char *name, *comment;
{
    char unique_name[500];
    char command[600];

   if (E->parallel.me==0) {
    sprintf(unique_name,"%06d.%s-%s",E->control.PID,comment,name);
    sprintf(command,"cp -f %s %s\n",name,unique_name);
    system(command);
    }

}


void thermal_buoyancy(E,buoy)
     struct All_variables *E;
     double **buoy;
    
{ 
    int i,m;
    double temp,*H;
    void phase_change_410();
    void phase_change_670();
    void phase_change_cmb();

    void remove_horiz_ave();

    H = (double *)malloc( (E->lmesh.noz+1)*sizeof(double));

    temp = E->control.Atemp;

    for(m=1;m<=E->sphere.caps_per_proc;m++)
      for(i=1;i<=E->lmesh.nno;i++)
        buoy[m][i] =  temp * E->T[m][i];

    if ( abs(E->control.Ra_410) > 1e-10)   {
      phase_change_410(E,E->Fas410,E->Fas410_b);

      for(m=1;m<=E->sphere.caps_per_proc;m++)
        for(i=1;i<=E->lmesh.nno;i++)
          buoy[m][i] -= E->control.Ra_410 * E->Fas410[m][i];
     }

    if ( abs(E->control.Ra_670) > 1e-10)   {
      phase_change_670(E,E->Fas670,E->Fas670_b);

      for(m=1;m<=E->sphere.caps_per_proc;m++)
        for(i=1;i<=E->lmesh.nno;i++)
          buoy[m][i] -= E->control.Ra_670 * E->Fas670[m][i];
     }

    if ( abs(E->control.Ra_cmb) > 1e-10)   {
      phase_change_cmb(E,E->Fascmb,E->Fascmb_b);

      for(m=1;m<=E->sphere.caps_per_proc;m++)
        for(i=1;i<=E->lmesh.nno;i++)
          buoy[m][i] -= E->control.Ra_cmb * E->Fascmb[m][i];
     }

    remove_horiz_ave(E,buoy,H,0);
    free ((void *) H);

    return;
}
 
double SIN_D(x)
     double x;
{
#if defined(__osf__)
  return sind(x);
#else
  return sin((x/180.0) * M_PI);
#endif

}

double COT_D(x)
     double x;
{
#if defined(__osf__)
  return cotd(x);
#else
  return tan(((90.0-x)/180.0) * M_PI);
#endif

}


/* non-runaway malloc */

void * Malloc1(bytes,file,line)
    int bytes;
    char *file;
    int line;
{
    void *ptr;

    ptr = malloc((size_t)bytes);
    if (ptr == (void *)NULL) {
	fprintf(stderr,"Memory: cannot allocate another %d bytes \n(line %d of file %s)\n",bytes,line,file);
	exit(0);
    }

    return(ptr);
}


/* Read in a file containing previous values of a field. The input in the parameter
   file for this should look like: `previous_name_file=string' and `previous_name_column=int' 
   where `name' is substituted by the argument of the function. 

   The file should have the standard CITCOM output format:
     # HEADER LINES etc
     index X Z Y ... field_value1 ...
     index X Z Y ... field_value2 ...
   where index is the node number, X Z Y are the coordinates and
   the field value is in the column specified by the abbr term in the function argument

   If the number of nodes OR the XZY coordinates for the node number (to within a small tolerance)
   are not in agreement with the existing mesh, the data is interpolated. 

   */

int read_previous_field(E,field,name,abbr)
    struct All_variables *E;
    float **field;
    char *name, *abbr;
{
    void fcopy_interpolating();
    int input_string();

    float cross2d();

    char discard[5001];
    char *token;
    char *filename;
    char *input_token;
    FILE *fp;
    int fnodesx,fnodesz,fnodesy;
    int i,j,column,found,m;
    int interpolate=0;

    float *X,*Z,*Y;

    filename=(char *)malloc(500*sizeof(char));
    input_token=(char *)malloc(1000*sizeof(char));

    /* Define field name, read parameter file to determine file name and column number */

    sprintf(input_token,"previous_%s_file",name);
    if(!input_string(input_token,filename,"initialize",E->parallel.me)) {
	fprintf(E->fp,"No previous %s information found in input file\n",name);fflush(E->fp);
	return(0);   /* if not found, take no further action, return zero */
    }

     
    fprintf(E->fp,"Previous %s information is in file %s\n",name,filename);fflush(E->fp);
 
    /* Try opening the file, fatal if this fails too */

    if((fp=fopen(filename,"r")) == NULL) {
	fprintf(E->fp,"Unable to open the required file `%s' (this is fatal)",filename);
	fflush(E->fp);

	exit(1);
    }
  
    
     /* Read header, get nodes xzy */

    fgets(discard,4999,fp);
    fgets(discard,4999,fp); 
    i=sscanf(discard,"# NODESX=%d NODESZ=%d NODESY=%d",&fnodesx,&fnodesz,&fnodesy);
    if(i<3) {
	fprintf(E->fp,"File %s is not in the correct format\n",filename);fflush(E->fp);
	exit(1);
    }

    fgets(discard,4999,fp); /* largely irrelevant line */
    fgets(discard,4999,fp);
    
    /* this last line is the column headers, we need to search for the occurence of abbr to
       find out the column to be read in */

    if(strtok(discard,"|")==NULL) { 
	fprintf(E->fp,"Unable to deciphre the columns in the input file");fflush(E->fp);
	exit(1);
    }

    found=0;
    column=1;

    while(found==0 && (token=strtok(NULL,"|")) != NULL) {
	if(strstr(token,abbr)!=0)
	    found=1;
	column++;
    }

    if(found) {
	fprintf(E->fp,"\t%s (%s) found in column %d\n",name,abbr,column);fflush(E->fp);
    }    
    else {
	fprintf(E->fp,"\t%s (%s) not found in file: %s\n",name,abbr,filename);fflush(E->fp);
	exit(1);
    }
    

  
    /* Another fatal condition (not suitable for interpolation: */
    if(((3!= E->mesh.nsd) && (fnodesy !=1)) || ((3==E->mesh.nsd) && (1==fnodesy))) {
	fprintf(E->fp,"Input data for file `%s'  is of inappropriate dimension (not %dD)\n",filename,E->mesh.nsd);fflush(E->fp);
	exit(1);
    }

    if(fnodesx != E->lmesh.nox || fnodesz != E->lmesh.noz || fnodesy != E->lmesh.noy) {
       fprintf(stderr,"wrong dimension in the input temperature file!!!!\n");
       exit(1);
       }

    X=(float *)malloc((2+fnodesx*fnodesz*fnodesy)*sizeof(float));
    Z=(float *)malloc((2+fnodesx*fnodesz*fnodesy)*sizeof(float));
    Y=(float *)malloc((2+fnodesx*fnodesz*fnodesy)*sizeof(float));
    
   /* Format for reading the input file (including coordinates) */

    sprintf(input_token," %%d %%e %%e %%e");
    for(i=5;i<column;i++)
	strcat(input_token," %*f");
    strcat(input_token," %f");


    for(m=1;m<=E->sphere.caps_per_proc;m++)
      for(i=1;i<=fnodesx*fnodesz*fnodesy;i++) {
	fgets(discard,4999,fp);
	sscanf(discard,input_token,&j,&(X[i]),&(Z[i]),&(Y[i]),&field[m][i]);
        }
    /* check consistency & need for interpolation */

    fclose(fp);


    free((void *)X);
    free((void *)Z);
    free((void *)Y);
    free((void *)filename);
    free((void *)input_token);
    
    return(1);
}


/* returns the out of plane component of the cross product of
   the two vectors assuming that one is looking AGAINST the 
   direction of the axis of D, anti-clockwise angles
   are positive (are you sure ?), and the axes are ordered 2,3 or 1,3 or 1,2 */


float cross2d(x11,x12,x21,x22,D)
    float x11,x12,x21,x22;
    int D;
{
  float temp;
   if(1==D)
       temp = ( x11*x22-x12*x21);
   if(2==D) 
       temp = (-x11*x22+x12*x21);
   if(3==D)
       temp = ( x11*x22-x12*x21);

   return(temp);
}

/* =================================================
  my version of arc tan
 =================================================*/

double myatan(y,x)
 double y,x;
 {
 double fi;

 fi = atan2(y,x);

 if (fi<0.0)
    fi += 2*M_PI;

 return(fi);
 }


double return1_test()
{
 return 1.0;
}
