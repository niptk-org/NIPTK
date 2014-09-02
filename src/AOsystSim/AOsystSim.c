#include <fitsio.h>
#include <unistd.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include "CLIcore.h"
#include "00CORE/00CORE.h"
#include "COREMOD_memory/COREMOD_memory.h"
#include "COREMOD_iofits/COREMOD_iofits.h"
#include "image_filter/image_filter.h"
#include "fft/fft.h"

#include "AOsystSim/AOsystSim.h"

extern DATA data;


// CLI commands
//
// function CLI_checkarg used to check arguments
// 1: float
// 2: long
// 3: string
// 4: existing image
//


int AOsystSim_simpleAOfilter_cli()
{
  if(CLI_checkarg(1,3)+CLI_checkarg(1,3)==0)
    AOsystSim_simpleAOfilter(data.cmdargtoken[1].val.string, data.cmdargtoken[2].val.string);

  return(0);
}



int AOsystSim_fitTelPup_cli()
{
  if(CLI_checkarg(1,4)==0)
    AOsystSim_fitTelPup(data.cmdargtoken[1].val.string);

  return(0);
}



int AOsystSim_run_cli()
{
  
  AOsystSim_run();

  return 0;
}





int init_AOsystSim()
{
  strcpy(data.module[data.NBmodule].name, __FILE__);
  strcpy(data.module[data.NBmodule].info, "conversion between image format, I/O");
  data.NBmodule++;

   strcpy(data.cmd[data.NBcmd].key,"AOsimfilt");
  strcpy(data.cmd[data.NBcmd].module,__FILE__);
  data.cmd[data.NBcmd].fp = AOsystSim_simpleAOfilter_cli;
  strcpy(data.cmd[data.NBcmd].info,"AO simple filtering");
  strcpy(data.cmd[data.NBcmd].syntax,"<input WF> <output WF>");
  strcpy(data.cmd[data.NBcmd].example,"AOsimfilt wfin wfout");
  strcpy(data.cmd[data.NBcmd].Ccall,"int AOsystSim_simpleAOfilter(char *IDin_name, char *IDout_name)");
  data.NBcmd++;

strcpy(data.cmd[data.NBcmd].key,"AOsystsfitpup");
  strcpy(data.cmd[data.NBcmd].module,__FILE__);
  data.cmd[data.NBcmd].fp = AOsystSim_fitTelPup_cli;
  strcpy(data.cmd[data.NBcmd].info,"fit telescope pupil");
  strcpy(data.cmd[data.NBcmd].syntax,"tel pupil file");
  strcpy(data.cmd[data.NBcmd].example,"AOsystfitpup");
  strcpy(data.cmd[data.NBcmd].Ccall,"AOsystSim_fitTelPup(char *ID_name)");
  data.NBcmd++;


  strcpy(data.cmd[data.NBcmd].key,"AOsystsim");
  strcpy(data.cmd[data.NBcmd].module,__FILE__);
  data.cmd[data.NBcmd].fp = AOsystSim_run_cli;
  strcpy(data.cmd[data.NBcmd].info,"run fake AO system");
  strcpy(data.cmd[data.NBcmd].syntax,"no argument");
  strcpy(data.cmd[data.NBcmd].example,"AOsystsim");
  strcpy(data.cmd[data.NBcmd].Ccall,"int AOsystSim_run()");
  data.NBcmd++;

 // add atexit functions here

  return 0;
}




/*! \brief simple AO filtering model using Fourier analysis
 *         simulates WFS integration, delay, noise (as a function of spatial frequency)
 *
 *  Open loop model
 * 
 *  AO simple filter is modeled by several 2D maps in spatial frequency:
 *  aosf_noise : noise per spatial frequency
 *  aosf_mult : signal throughput per spatial frequency
 *  aosf_gain : loop gain to be applied per spatial frequency
 */


int AOsystSim_simpleAOfilter(char *IDin_name, char *IDout_name)
{
    long IDin, IDmask, IDout, IDdm, IDwfe;
    long *sizearray;
    long cnt0;
    long ii, jj;
    double x, y, r;
    long ID, IDin1;

    long IDaosf_noise;
    long IDaosf_mult;
    long IDaosf_gain;

    float loopgain = 0.001;
    float multr0 = 0.2;
    
    float wfsetime = 0.001; /**< WFS exposure time */
	float timedelay = 0.0005;/**< time delay between wavefront sensor measurement (end of exposure) and DM correction [sec] */
	long size2;
	double tnowdouble = 0.0;
	double time_wfse0 = 0.0;
	long wfsecnt = 0;

	double rmask;
	double r1;
	double WFrms0 = 0.0;
	double WFrms = 0.0;
	long WFrmscnt = 0;
	
	double noiselevel = 0.3; // noise level per WFS measurement [um] - only white noise is currently supported
	
	/** wavefront correction buffer to implement time delay */
	long IDwfcbuff;
	long wfcbuff_size = 10000;
	double *wfcbuff_time;
	long *wfcbuff_status; // 1 : waiting to be applied
	long k, k0, k1;
	long wfsecnt1;
	
	double dmmovetime = 0.001; /**< time it takes for DM to move */
	long dmmoveNBpt = 10;
	
    sizearray = (long*) malloc(sizeof(long)*2);

    IDin = read_sharedmem_image(IDin_name);  /**< turbulence channel */
    sizearray[0] = data.image[IDin].md[0].size[0];
    sizearray[1] = data.image[IDin].md[0].size[1];
	size2 = sizearray[0]*sizearray[1];

	IDwfcbuff = create_3Dimage_ID("wfcbuff", sizearray[0], sizearray[1], wfcbuff_size);
	wfcbuff_time = (double*) malloc(sizeof(double)*wfcbuff_size);
	wfcbuff_status = (long*) malloc(sizeof(long)*wfcbuff_size);
	for(k=0;k<wfcbuff_size;k++)
		wfcbuff_status[k] = 0;

	rmask = 0.4*sizearray[0];

    IDaosf_noise = create_2Dimage_ID("aosf_noise", sizearray[0], sizearray[1]);
    IDaosf_mult = create_2Dimage_ID("aosf_mult", sizearray[0], sizearray[1]);
    IDaosf_gain = create_2Dimage_ID("aosf_gain", sizearray[0], sizearray[1]);

	IDmask = create_2Dimage_ID("aosf_mask", sizearray[0], sizearray[1]);
	

    for(ii=0; ii<sizearray[0]; ii++)
        for(jj=0; jj<sizearray[1]; jj++)
        {
            x = 1.0*ii-0.5*sizearray[0];
            y = 1.0*jj-0.5*sizearray[1];
            r = sqrt(x*x+y*y);
			r1 = (r-rmask)/(0.5*sizearray[0]-rmask);
			
			if(r1<0.0)
				data.image[IDmask].array.F[jj*sizearray[0]+ii] = 1.0;
			else if (r1>1.0)
				data.image[IDmask].array.F[jj*sizearray[0]+ii] = 0.0;
			else
				data.image[IDmask].array.F[jj*sizearray[0]+ii] = 0.5*(cos(r1*M_PI)+1.0);
			
            data.image[IDaosf_gain].array.F[jj*sizearray[0]+ii] = loopgain;
            data.image[IDaosf_mult].array.F[jj*sizearray[0]+ii] = exp(-pow(r/(multr0*sizearray[0]),8.0));
            if(r>multr0*sizearray[0])
				data.image[IDaosf_mult].array.F[jj*sizearray[0]+ii] = 0.0;
            data.image[IDaosf_noise].array.F[jj*sizearray[0]+ii] = 0.0;
        }
    save_fits("aosf_noise", "!aosf_noise.fits");
    save_fits("aosf_mult", "!aosf_mult.fits");
    save_fits("aosf_gain", "!aosf_gain.fits");

	permut("aosf_mult");
	permut("aosf_noise");
	permut("aosf_gain");

	
	

    IDdm = create_image_ID("aofiltdm", 2, sizearray, FLOAT, 1, 0);
    IDwfe = create_image_ID("aofiltwfe", 2, sizearray, FLOAT, 1, 0);

    IDout = create_image_ID(IDout_name, 2, sizearray, FLOAT, 1, 0);
	strcpy(data.image[IDout].kw[0].name, "TIME");	
	data.image[IDout].kw[0].type = 'D';
	data.image[IDout].kw[0].value.numf = 0.0;
	strcpy(data.image[IDout].kw[0].comment, "Physical time [sec]");
    
	IDin1 = create_image_ID("aofiltin", 2, sizearray, FLOAT, 1, 0);
 
    printf("%s -> %s\n", IDin_name, IDout_name);
    cnt0 = -1;
    time_wfse0 = data.image[IDin].kw[0].value.numf; /** start of WFS exposure */
    wfsecnt = 0;
    wfsecnt1 = 0;
    while(1)
    {
		usleep(10);
        if(data.image[IDin].md[0].cnt0!=cnt0)
        {
			/** input masking */
			for(ii=0; ii<size2; ii++)
                data.image[IDin1].array.F[ii] = data.image[IDin].array.F[ii] * data.image[IDmask].array.F[ii];
			
			/** Wavefront correction & measurement */
			WFrms0 = 0.0;
			WFrms = 0.0;
			WFrmscnt = 0;
            for(ii=0; ii<sizearray[0]*sizearray[1]; ii++)
            {
                data.image[IDout].array.F[ii] = data.image[IDin1].array.F[ii] - data.image[IDdm].array.F[ii];
				if(data.image[IDmask].array.F[ii]>0.99)
					{
						WFrms0 += data.image[IDin1].array.F[ii]*data.image[IDin1].array.F[ii];
						WFrms += data.image[IDout].array.F[ii]*data.image[IDout].array.F[ii];
						WFrmscnt ++;
					}
			}
			WFrms0 = sqrt(WFrms0/WFrmscnt);
			WFrms = sqrt(WFrms/WFrmscnt);
			data.image[IDout].kw[0].value.numf = tnowdouble;

 
 			tnowdouble = data.image[IDin].kw[0].value.numf;
            printf("\r Time : %.6lf    WFSexposure time:  %.6f   [%5ld]    %10.5lf -> %10.5lf ", tnowdouble, tnowdouble-time_wfse0, wfsecnt, WFrms0, WFrms);
            fflush(stdout);
            cnt0 = data.image[IDin].md[0].cnt0;

            do2drfft(IDout_name, "aosf_tmpfft");
            ID = image_ID("aosf_tmpfft");

            for(ii=0; ii<sizearray[0]*sizearray[1]; ii++)
            {
                data.image[ID].array.CF[ii].re *= data.image[IDaosf_mult].array.F[ii]/size2;
                data.image[ID].array.CF[ii].im *= data.image[IDaosf_mult].array.F[ii]/size2;
            }

            do2dffti("aosf_tmpfft", "testo");
            delete_image_ID("aosf_tmpfft");
            ID = image_ID("testo");
          
			/** Wavefront estimation */
             for(ii=0; ii<sizearray[0]*sizearray[1]; ii++)
                data.image[IDwfe].array.F[ii] += data.image[ID].array.CF[ii].re;
			delete_image_ID("testo");       
			wfsecnt ++;
			
			if((tnowdouble-time_wfse0) > wfsetime)
			{
				if(wfsecnt>0)
					for(ii=0; ii<sizearray[0]*sizearray[1]; ii++)
						data.image[IDwfe].array.F[ii] /= wfsecnt;
	
				
				/** write entries in buffer */
				for(k1=0;k1<dmmoveNBpt;k1++)
				{
				k0 = 0;
				while((wfcbuff_status[k0]==1)&&(k0<wfcbuff_size))
					k0++;
				if(k0>wfcbuff_size-1)
				{
					printf("\n WFC buffer full [%ld]\n", k0);
					exit(0);
				}
						
				for(ii=0; ii<size2; ii++)
					data.image[IDwfcbuff].array.F[k0*size2+ii] = data.image[IDwfe].array.F[ii]/dmmoveNBpt;
				wfcbuff_time[k0] = tnowdouble + timedelay + 1.0*k1/(dmmoveNBpt-1)*dmmovetime;
				wfcbuff_status[k0] = 1;
				}
				
				for(ii=0; ii<sizearray[0]*sizearray[1]; ii++)
							data.image[IDwfe].array.F[ii] = 0.0;
						time_wfse0 += wfsetime;
						wfsecnt = 0;
									wfsecnt1++;		
			}
				
				
				
			for(k=0; k<wfcbuff_size; k++)
				{
					if(wfcbuff_status[k]==1)
						if(wfcbuff_time[k]<tnowdouble)
							{
					//			printf("Reading WFC buffer slice %ld  [%5ld %5ld]\n", k, wfsecnt1, wfsecnt);
								/** update DM shape */
								for(ii=0; ii<sizearray[0]*sizearray[1]; ii++)
									data.image[IDdm].array.F[ii] += data.image[IDaosf_gain].array.F[ii]*data.image[IDwfcbuff].array.F[k*size2+ii];
								wfcbuff_status[k] = 0;
							}		
				}	
            data.image[IDout].md[0].cnt0 = cnt0;
        }
    }
	
	delete_image_ID("circbuff");
	
    free(sizearray);
	free(wfcbuff_time);
	free(wfcbuff_status);
	
    return(0);
}



















// all sizes in actuators on DM

long AOsystSim_mkTelPupDM(char *ID_name, long msize, double xc, double yc, double rin, double rout, double pupPA, double spiderPA, double spideroffset, double spiderthick)
{
  long ID, IDz;
  long ii,  jj;
  double x, y, x1, y1, r;
  long binfact = 8;
  long size;
  double PA;
  double val;
  long IDindex, IDi;
  long index;
  long ii1, jj1;


  size = msize*binfact;
  
  ID = create_2Dimage_ID("TPmask", msize, msize);
  IDi = create_3Dimage_ID("TPind", msize, msize, 5);

  IDz = create_2Dimage_ID("telpupDMz", size, size);
  IDindex = create_2Dimage_ID("telpupDMzindex", size, size);
  for(ii=0;ii<size;ii++)
    for(jj=0;jj<size;jj++)
      {
	index = 0;
	val = 1.0;

	x = (1.0*ii/binfact);
	y = (1.0*jj/binfact);
	x -= xc;
	y -= yc;
	r = sqrt(x*x+y*y);
	PA = atan2(y, x);
	PA += pupPA;
	x = r*cos(PA);
	y = r*sin(PA);

	if(r>rout)
	  val = 0.0;
	if(r<rin)
	  val = 0.0;
	
	x1 = x-spideroffset-spiderthick/2.0;
	y1 = y;
	PA = atan2(y1,x1);
	if(fabs(PA)<spiderPA)
	  index = 1;


	x1 = x+spideroffset+spiderthick/2.0;
	y1 = y;
	PA = atan2(y1,-x1);
	if(fabs(PA)<spiderPA)
	  index = 2;
	



	x1 = x+spideroffset-spiderthick/2.0;
	y1 = y;
	PA = atan2(x1,y1);
	if((fabs(PA)<M_PI/2-spiderPA)&&(x<0))
	  index = 3;
	
	x1 = -x+spideroffset-spiderthick/2.0;
	y1 = y;
	PA = atan2(x1,y1);
	if((fabs(PA)<M_PI/2-spiderPA)&&(x>0))
	  index = 3;
	




	x1 = x+spideroffset-spiderthick/2.0;
	y1 = -y;
	PA = atan2(x1,y1);
	if((fabs(PA)<M_PI/2-spiderPA)&&(x<0))
	  index = 4;

	x1 = -x+spideroffset-spiderthick/2.0;
	y1 = -y;
	PA = atan2(x1,y1);
	if((fabs(PA)<M_PI/2-spiderPA)&&(x>0))
	  index = 4;

	if(index==0)
	  val = 0.0;
	data.image[IDz].array.F[jj*size+ii] = val;
	data.image[IDindex].array.F[jj*size+ii] = index*val;

	ii1 = (long) (ii/binfact);
	jj1 = (long) (jj/binfact);
	

	data.image[ID].array.F[jj1*msize+ii1] += val/binfact/binfact;
	
	if(val>0.5)
	  {
	    data.image[IDi].array.F[jj1*msize+ii1] = 1;
	    data.image[IDi].array.F[index*msize*msize+jj1*msize+ii1] = 1;
	  }
      }
  
  //  save_fits("telpupDMz", "!telpupDMz.fits");
  //save_fits("telpupDMzindex", "!telpupDMzindex.fits");  
  delete_image_ID("telpupDMz");
  delete_image_ID("telpupDMzindex");

  save_fits("TPmask", "!TPmask.fits");
  save_fits("TPind", "!TPind.fits");

  return(ID);
}


long AOsystSim_fitTelPup(char *ID_name)
{
  long ID;
  double xc, yc, pupPA, spiderPA, spideroffset, spiderthick, rin, rout;
  long size;

  rout = 21.99;
  rin = 0.315*rout;
  pupPA = -0.105;
  spiderPA = 0.9;
  spiderthick = 0.06*rout;
  spideroffset = 0.165*rout;

  xc = 24.87;
  yc = 24.06;

  
  AOsystSim_mkTelPupDM("testpup", 50, xc, yc, rin, rout, pupPA, spiderPA, spideroffset, spiderthick);

  /*  ID = image_ID(ID_name);
  size = data.image[ID].md[0].size[0];

  xc = 79.7;
  yc= 71.125;

  rout = 47.5;
  rin = 0.315*rout;
  pupPA = -0.105;
  spiderPA = 0.9;
  spiderthick = 0.06*rout;
  spideroffset = 0.165*rout;

  AOsystSim_mkTelPupDM("testpup", size, xc, yc, rin, rout, pupPA, spiderPA, spideroffset, spiderthick);
  */
  //  save_fits("TPmask", "!testpup.fits");

  return(ID);
}





int AOsystSim_run()
{
  long dmxsize = 50;
  long dmysize = 50;
  long *dmsize;
  long twait = 1; // us
  long long cnt = 0;
  long long cnt0;
  float lambda = 0.7; // um


  long pupsize = 256;
  double puprad = 22.0;
  long IDpupa, IDpupp, IDpuppIR;
  long IDpyrpha, IDpyramp;
  double x, y, r;
  long ii, jj, ii1, jj1;
  double pcoeff = 0.75;
  long pupsize2;
  long ID, IDa, IDp;
  double lenssize = 1200.0;

  long wfssize = 120;
  long *wfssize_array;
  long ID_wfs, ID_wfe;
  long offset, offset1;
  long IDflat, IDdmdisp;

  long IDatmpha;
  long iioffset, jjoffset;
  long IDpuppc, IDpuppcIR;
  long IDpsf, IDpsfIR;
  long *pupsize_array;

  float alphaCorr = 0.8;
  long IDdmt, IDdmtg;

  long moffset;
  long msize;
  long usleeptime = 1000; // delay at each loop

/** Pyramid modulation */
	long modpt;
	long NBmodpt = 32;
	double modr = 0.1;
	double modPA, modx, mody;
long IDwfscumul;

  pupsize2 = pupsize*pupsize;


  printf("Running fake AO system simulation\n");



  dmsize = (long*) malloc(sizeof(long)*2);
  wfssize_array = (long*) malloc(sizeof(long)*2);
  pupsize_array = (long*) malloc(sizeof(long)*2);
   
  
  // INITIALIZE
  
  // create wavefront error input
  dmsize[0] = dmxsize;
  dmsize[1] = dmysize;
  ID_wfe = create_2Dimage_ID("pyrwfeinput", dmxsize, dmysize); // input WF error, to DM sampling 


/*
  IDflat = read_sharedmem_image("dmdisp0"); // flat
  data.image[IDflat].md[0].write = 1;
  for(ii=0;ii<dmxsize;ii++)
    for(jj=0;jj<dmysize;jj++)
      data.image[IDflat].array.F[jj*dmxsize+ii] = 0.5;
  data.image[IDflat].md[0].cnt0++; 
  data.image[IDflat].md[0].write = 0;
*/

  // create_image_ID("dm_wfe_sim", 2, dmsize, FLOAT, 1, 0);  
 
  // create PSF images
  pupsize_array[0] = pupsize;
  pupsize_array[1] = pupsize;
  IDpsf = create_image_ID("aosimpsf", 2, pupsize_array, FLOAT, 1, 0);  
  IDpsfIR = create_image_ID("aosimpsfIR", 2, pupsize_array, FLOAT, 1, 0);  



  IDatmpha = read_sharedmem_image("outfiltwf"); // [um]
  

  

  IDdmdisp = read_sharedmem_image("dmdisp");

  // create DM
  dmsize[0] = dmxsize;
  dmsize[1] = dmysize;


  // create WFS image
  wfssize_array[0] = wfssize;
  wfssize_array[1] = wfssize;
  wfssize_array[2] = 3; // number of slices in rolling buffer
  ID_wfs = create_image_ID("wfs_sim", 3, wfssize_array, FLOAT, 1, 0);
  data.image[ID_wfs].md[0].cnt1 = 0; // next slice to write

  IDpuppc = create_image_ID("puppcrop", 2, dmsize, FLOAT, 1, 0);


  
  IDpyrpha = create_2Dimage_ID("pyrpha0", pupsize, pupsize);
  IDpyramp = create_2Dimage_ID("pyramp", pupsize, pupsize);
  for(ii=0;ii<pupsize;ii++)
    for(jj=0;jj<pupsize;jj++)
      {
	x = 1.0*(ii-pupsize/2);
	y = 1.0*(jj-pupsize/2);

	data.image[IDpyrpha].array.F[jj*pupsize+ii] = pcoeff*(fabs(x)+fabs(y));
	if((fabs(x)>lenssize)||(fabs(y)>lenssize))
	  data.image[IDpyramp].array.F[jj*pupsize+ii] = 0.0;
	else
	  data.image[IDpyramp].array.F[jj*pupsize+ii] = 1.0;
      }
  gauss_filter("pyrpha0", "pyrpha", 15.0, 20);
  IDpyrpha = image_ID("pyrpha");
  save_fits("pyrpha","!pyrpha.fits");

  offset1 = (pupsize-dmxsize)/2;
  printf("OFFSET = %ld\n", offset);
  cnt = -1;
  printf("\n");


  if(ID=image_ID("TpupMask")==-1)
    {
      IDpupa = make_disk("pupa", pupsize, pupsize, 0.5*pupsize, 0.5*pupsize, puprad);
      for(ii=0;ii<pupsize;ii++)
	for(jj=0;jj<pupsize;jj++)
	  {
	    x = (1.0*ii-0.5*pupsize)/puprad;
	    y = (1.0*jj-0.5*pupsize)/puprad;
	    r = sqrt(x*x+y*y);
	    if(r<0.3)
	      data.image[IDpupa].array.F[jj*pupsize+ii] = 0.0;
	    if((fabs(0.5*x+0.5*y)<0.05)||(fabs(0.5*x-0.5*y)<0.05))
		  data.image[IDpupa].array.F[jj*pupsize+ii] = 0.0;
	  }
    } 
  else
    {      
      msize = data.image[ID].md[0].size[0];
      IDpupa = make_disk("pupa", pupsize, pupsize, 0.5*pupsize, 0.5*pupsize, puprad);
      offset = (pupsize-msize)/2;
      for(ii=0;ii<msize;ii++)
	for(jj=0;jj<msize;jj++)
	  data.image[IDpupa].array.F[(jj+offset)*pupsize+(ii+offset)] = data.image[ID].array.F[jj*msize+ii];	    
    }
 
  //  save_fits("pupa", "!Tpupa.fits");

	IDwfscumul = create_2Dimage_ID("wfscumul", pupsize, pupsize);

  while(1)
    {
      usleep(usleeptime);
      
  for(ii=0;ii<pupsize2;ii++)
		data.image[IDwfscumul].array.F[ii] = 0.0;

      for(modpt=0; modpt<NBmodpt; modpt++)
      {
      // IMPORT WF ERROR
      iioffset = 6;
      jjoffset = 6;
      for(ii=0;ii<dmxsize;ii++)
		for(jj=0;jj<dmysize;jj++)
	  {
	    data.image[ID_wfe].array.F[jj*dmxsize+ii] = 0.0;
	    ii1 = ii+iioffset;
	    jj1 = jj+jjoffset;
	    data.image[ID_wfe].array.F[jj*dmxsize+ii] = data.image[IDatmpha].array.F[jj1*data.image[IDatmpha].md[0].size[0]+ii1]; // um
	    
	  }
	        
      data.image[ID_wfe].md[0].cnt0++;
      data.image[ID_wfe].md[0].write = 0;


   	IDpupp = create_2Dimage_ID("pupp", pupsize, pupsize);
	IDpuppIR = create_2Dimage_ID("puppIR", pupsize, pupsize);

	

	  data.image[IDpuppc].md[0].write==1;

	  for(ii=0;ii<dmxsize;ii++)
	  for(jj=0;jj<dmysize;jj++)
	    data.image[IDpuppc].array.F[jj*dmxsize+ii] = data.image[ID_wfe].array.F[jj*dmxsize+ii] - 2.0*data.image[IDdmdisp].array.F[jj*dmxsize+ii] + modr*cos(2.0*M_PI*modpt/NBmodpt)*(1.0*ii-0.5*dmxsize) + modr*sin(2.0*M_PI*modpt/NBmodpt)*(1.0*jj-0.5*dmysize);   // [um]
	    
	  for(ii=0;ii<dmxsize;ii++)
	    for(jj=0;jj<dmysize;jj++)
	      data.image[IDpupp].array.F[(jj+offset1)*pupsize+(ii+offset1)] = data.image[IDpuppc].array.F[jj*dmxsize+ii]/lambda*M_PI*2.0;  // [rad]
	  
	  data.image[IDpuppc].md[0].cnt0++;
	  data.image[IDpuppc].md[0].write = 0;
	  
	  for(ii=0;ii<dmxsize;ii++)
	    for(jj=0;jj<dmysize;jj++)
	      data.image[IDpuppIR].array.F[(jj+offset1)*pupsize+(ii+offset1)] = (lambda/1.65)*data.image[IDpuppc].array.F[jj*dmxsize+ii]/lambda*M_PI*2.0;
	  



	  mk_complex_from_amph("pupa","pupp","pupc");
	  permut("pupc");
	  do2dfft("pupc","focc");
	  delete_image_ID("pupc");
	  permut("focc");
	  mk_amph_from_complex("focc", "foca", "focp");
	  
	  delete_image_ID("focc");
	  IDp = image_ID("focp");
	  IDa = image_ID("foca");
	 


	  data.image[IDpsf].md[0].write = 1;
	  for(ii=0;ii<pupsize2;ii++)
	    {
	      data.image[IDpsf].array.F[ii] = data.image[IDa].array.F[ii]*data.image[IDa].array.F[ii];
	      data.image[IDa].array.F[ii] *= data.image[IDpyramp].array.F[ii];
	      data.image[IDp].array.F[ii] += data.image[IDpyrpha].array.F[ii];
	    }
	  data.image[IDpsf].md[0].cnt0++;
	  data.image[IDpsf].md[0].write = 0;


	  mk_complex_from_amph("pupa","puppIR","pupc");
	  permut("pupc");
	  do2dfft("pupc","focc");
	  delete_image_ID("pupc");
	  permut("focc");
	  mk_amph_from_complex("focc", "focaIR", "focpIR");
	  
	  delete_image_ID("focc");
	  delete_image_ID("focpIR");
	  IDa = image_ID("focaIR");
	  

	  data.image[IDpsfIR].md[0].write = 1;
	  for(ii=0;ii<pupsize2;ii++)
	    data.image[IDpsfIR].array.F[ii] = data.image[IDa].array.F[ii]*data.image[IDa].array.F[ii];
	  data.image[IDpsfIR].md[0].cnt0++;
	  data.image[IDpsfIR].md[0].write = 0;




	  mk_complex_from_amph("foca","focp","focc1");
	  delete_image_ID("foca");
	  delete_image_ID("focp");
	  permut("focc1");
	  do2dfft("focc1","pupc1");
	  delete_image_ID("focc1");
	  permut("pupc1");
	  mk_amph_from_complex("pupc1", "pupa1", "pupp1");
	  delete_image_ID("pupc1");
	  delete_image_ID("pupp1");
	  
	  ID = image_ID("pupa1");
	  offset = (pupsize-wfssize)/2;
	  
	  for(ii=0;ii<pupsize2;ii++)
		data.image[IDwfscumul].array.F[ii] += data.image[ID].array.F[ii]*data.image[ID].array.F[ii];
	delete_image_ID("pupa1");
	  }
	  
	  
	  data.image[ID_wfs].md[0].write = 1;
	  moffset = data.image[ID_wfs].md[0].cnt1*wfssize*wfssize;
	  	  for(ii1=0;ii1<wfssize;ii1++)
	    for(jj1=0;jj1<wfssize;jj1++)	     
	      data.image[ID_wfs].array.F[moffset+jj1*wfssize+ii1] = data.image[IDwfscumul].array.F[(jj1+offset)*pupsize+ii1+offset]; //*data.image[ID].array.F[(jj1+offset)*pupsize+ii1+offset];
	  data.image[ID_wfs].md[0].cnt1++;
	  if(data.image[ID_wfs].md[0].cnt1==data.image[ID_wfs].md[0].size[2])
	    data.image[ID_wfs].md[0].cnt1 = 0;
	  data.image[ID_wfs].md[0].cnt0++;
	  data.image[ID_wfs].md[0].write = 0;


	  printf("\r%10ld       ", data.image[ID_wfs].md[0].cnt0);
	  fflush(stdout);
	  //  cnt = cnt0;
	  //	}
    }
      
      delete_image_ID("wfscumul");
  free(dmsize);
  free(wfssize_array);
  free(pupsize_array);


  return 0;
}
