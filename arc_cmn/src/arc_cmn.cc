/*********************************************************************************
 *  ARC-SRTK - Single Frequency RTK Pisitioning Library
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  Created on: July 07, 2017
 *********************************************************************************/

#define _POSIX_C_SOURCE 199506
#include <stdarg.h>
#include <ctype.h>

#ifndef WIN32
#include <dirent.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>

#endif
#include "arc.h"
#include "glog/logging.h"

#define AS2R        (D2R/3600.0)    /* arc sec to radian */
#define GME         3.986004415E+14 /* earth gravitational constant */
#define GMS         1.327124E+20    /* sun gravitational constant */
#define GMM         4.902801E+12    /* moon gravitational constant */
#define SQR(x)      ((x)*(x))
#define EPS           0.000001
#define ITERS         60
/* function prototypes -------------------------------------------------------*/
#ifdef IERS_MODEL
extern int dehanttideinel_(double *xsta, int *year, int *mon, int *day,
                           double *fhr, double *xsun, double *xmon,
                           double *dxtide);
#endif

static const double gpst0[]={1980,1, 6,0,0,0}; /* gps time reference */
static const double gst0 []={1999,8,22,0,0,0}; /* galileo system time reference */
static const double bdt0 []={2006,1, 1,0,0,0}; /* beidou time reference */

static double leaps[MAXLEAPS+1][7]={ /* leap seconds (y,m,d,h,m,s,utc-gpst) */
    {2017,1,1,0,0,0,-18},
    {2015,7,1,0,0,0,-17},
    {2012,7,1,0,0,0,-16},
    {2009,1,1,0,0,0,-15},
    {2006,1,1,0,0,0,-14},
    {1999,1,1,0,0,0,-13},
    {1997,7,1,0,0,0,-12},
    {1996,1,1,0,0,0,-11},
    {1994,7,1,0,0,0,-10},
    {1993,7,1,0,0,0, -9},
    {1992,7,1,0,0,0, -8},
    {1991,1,1,0,0,0, -7},
    {1990,1,1,0,0,0, -6},
    {1988,1,1,0,0,0, -5},
    {1985,7,1,0,0,0, -4},
    {1983,7,1,0,0,0, -3},
    {1982,7,1,0,0,0, -2},
    {1981,7,1,0,0,0, -1},
    {0}
};
const double chisqr[100]={      /* chi-sqr(n) (alpha=0.001) */
    10.8,13.8,16.3,18.5,20.5,22.5,24.3,26.1,27.9,29.6,
    31.3,32.9,34.5,36.1,37.7,39.3,40.8,42.3,43.8,45.3,
    46.8,48.3,49.7,51.2,52.6,54.1,55.5,56.9,58.3,59.7,
    61.1,62.5,63.9,65.2,66.6,68.0,69.3,70.7,72.1,73.4,
    74.7,76.0,77.3,78.6,80.0,81.3,82.6,84.0,85.4,86.7,
    88.0,89.3,90.6,91.9,93.3,94.7,96.0,97.4,98.7,100 ,
    101 ,102 ,103 ,104 ,105 ,107 ,108 ,109 ,110 ,112 ,
    113 ,114 ,115 ,116 ,118 ,119 ,120 ,122 ,123 ,125 ,
    126 ,127 ,128 ,129 ,131 ,132 ,133 ,134 ,135 ,137 ,
    138 ,139 ,140 ,142 ,143 ,144 ,145 ,147 ,148 ,149
};
const double lam_carr[MAXFREQ]={ /* carrier wave length (m) */
    CLIGHT/FREQ1,CLIGHT/FREQ2,CLIGHT/FREQ5,CLIGHT/FREQ6,CLIGHT/FREQ7,
    CLIGHT/FREQ8,CLIGHT/FREQ9
};
/* todo:when the Ratio value is 1.5,the effect is better than 3.0 */
const prcopt_t prcopt_default={ /* defaults processing options */
    PMODE_KINEMA,0,1,SYS_GPS|SYS_CMP,   /* mode,soltype,nf,navsys */
    15.0*D2R,{{0,0}},           /* elmin,snrmask */
    0,1,1,1,                    /* sateph,modear,glomodear,bdsmodear */
    10,5,5,1,                   /* maxout,minlock,minfix,armaxiter */
    0,0,0,0,                    /* estion,esttrop,dynamics,tidecorr */
    1,0,0,0,0,                  /* niter,codesmooth,intpref,sbascorr,sbassatsel */
    0,0,                        /* rovpos,refpos */
    {100.0,100.0},              /* eratio[] */
    {100.0,0.003,0.003,0.0,1.0}, /* err[] */
    {30.0,0.03,0.3},            /* std[] */
    {1E-1,1E-2,1E-2,1E-1,1E-2,0.5}, /* prn[] */
    5E-12,                      /* sclkstab */
    {1.4,0.9999,0.25,0.1,0.05}, /* thresar */
    0.0,0.0,0.05,               /* elmaskar,almaskhold,thresslip */
    30.0,15.0,30.0,             /* maxtdif,maxinno,maxgdop */
    {0},{0},{0},                /* baseline,ru,rb */
    {"",""},                    /* anttype */
    {{0}},{{0}},{0}             /* antdel,pcv,exsats */
};
const solopt_t solopt_default={ /* defaults solution output options */
    SOLF_LLH,TIMES_GPST,1,3,    /* posf,times,timef,timeu */
    0,1,0,0,0,0,                /* degf,outhead,outopt,datum,height,geoid */
    0,0,0,                      /* solstatic,sstat,arc_log */
    {0.0,0.0},                  /* nmeaintv */
    " ",""                      /* separator/program name */
};

const filopt_t fileopt_default = {
	"",   /* satellite antenna parameters file */
    "",   /* receiver antenna parameters file */
    "",   /* station positions file */
    "",   /* external geoid data file */
    "",   /* ionosphere data file */
    "",   /* dcb data file */
    "",   /* eop data file */
    "",   /* ocean tide loading blq file */
    "",   /* ftp/http temporaly directory */
    "",   /* google earth exec file */
    "",   /* solution statistics file */
    ""    /* debug arc_log file */
};

static char *obscodes[]={       /* observation code strings */
    
    ""  ,"1C","1P","1W","1Y", "1M","1N","1S","1L","1E", /*  0- 9 */
    "1A","1B","1X","1Z","2C", "2D","2S","2L","2X","2P", /* 10-19 */
    "2W","2Y","2M","2N","5I", "5Q","5X","7I","7Q","7X", /* 20-29 */
    "6A","6B","6C","6X","6Z", "6S","6L","8L","8Q","8X", /* 30-39 */
    "2I","2Q","6I","6Q","3I", "3Q","3X","1I","1Q","5A"  /* 40-49 */
    "5B","5C","9A","9B","9C", "9X",""  ,""  ,""  ,""    /* 50-59 */
};
static unsigned char obsfreqs[]={
    /* 1:L1/E1, 2:L2/B1, 3:L5/E5a/L3, 4:L6/LEX/B3, 5:E5b/B2, 6:E5(a+b), 7:S */
    0, 1, 1, 1, 1,  1, 1, 1, 1, 1, /*  0- 9 */
    1, 1, 1, 1, 2,  2, 2, 2, 2, 2, /* 10-19 */
    2, 2, 2, 2, 3,  3, 3, 5, 5, 5, /* 20-29 */
    4, 4, 4, 4, 4,  4, 4, 6, 6, 6, /* 30-39 */
    2, 2, 4, 4, 3,  3, 3, 1, 1, 3, /* 40-49 */
    3, 3, 7, 7, 7,  7, 0, 0, 0, 0  /* 50-59 */
};
static char *bds_geo[]={    /* bds GEO satellite list */
    "C01","C03","C04","C05",""
};
static char *bds_igso[]={   /* bds IGSO satellite list */
    "C06","C07","C08","C09","C10",""
};
static char* bds_meo[]={    /* bds MEO satellite */
    "C11","C12",""
};
static char codepris[7][MAXFREQ][16]={  /* code priority table */
   
   /* L1/E1      L2/B1        L5/E5a/L3 L6/LEX/B3 E5b/B2    E5(a+b)  S */
    {"CPYWMNSL","PYWCMNDSLX","IQX"     ,""       ,""       ,""      ,""    }, /* GPS */
    {"PC"      ,"PC"        ,"IQX"     ,""       ,""       ,""      ,""    }, /* GLO */
    {"CABXZ"   ,""          ,"IQX"     ,"ABCXZ"  ,"IQX"    ,"IQX"   ,""    }, /* GAL */
    {"CSLXZ"   ,"SLX"       ,"IQX"     ,"SLX"    ,""       ,""      ,""    }, /* QZS */
    {"C"       ,""          ,"IQX"     ,""       ,""       ,""      ,""    }, /* SBS */
    {"IQX"     ,"IQX"       ,"IQX"     ,"IQX"    ,"IQX"    ,""      ,""    }, /* BDS */
    {""        ,""          ,"ABCX"    ,""       ,""       ,""      ,"ABCX"}  /* IRN */
};
static const double amt_avg[5][6]={{15.0,1013.25,299.65,26.31,6.30E-3,2.77},
                                   {30.0,1017.25,294.15,21.79,6.05E-3,3.15},
                                   {45.0,1015.75,283.15,11.66,5.58E-3,2.57},
                                   {60.0,1011.75,272.15, 6.78,5.39E-3,1.81},
                                   {75.0,1013.00,263.65, 4.11,4.53E-3,1.55}};
static const double amt_amp[5][6]={{15.0,0.0,0.0,0.0,0.0,0.0},
                                   {30.0,-3.75,7.0,8.85,0.25E-3,0.33},
                                   {45.0,-2.25,11.0,7.24,0.32E-3,0.46},
                                   {60.0,-1.75,15.0,5.36,0.81E-3,0.74},
                                   {75.0,-0.50,14.5,3.39,0.62E-3,0.30}};
static fatalfunc_t *fatalfunc=NULL; /* fatal callback function */

#ifdef IERS_MODEL
extern int gmf_(double *mjd, double *lat, double *lon, double *hgt, double *zd,
                double *gmfh, double *gmfw);
#endif

/* fatal error ---------------------------------------------------------------*/
static void fatalerr(const char *format, ...)
{
    char msg[1024];
    va_list ap;
    va_start(ap,format); vsprintf(msg,format,ap); va_end(ap);
    if (fatalfunc) fatalfunc(msg);
    else fprintf(stderr,"%s",msg);
    exit(-9);
}
/* add fatal callback function -------------------------------------------------
* add fatal callback function for arc_mat(),arc_zeros(),arc_imat()
* args   : fatalfunc_t *func I  callback function
* return : none
* notes  : if malloc() failed in return : none
*-----------------------------------------------------------------------------*/
extern void arc_add_fatal(fatalfunc_t *func)
{
    fatalfunc=func;
}
/* satellite system+prn/slot number to satellite number ------------------------
* convert satellite system+prn/slot number to satellite number
* args   : int    sys       I   satellite system (SYS_GPS,SYS_GLO,...)
*          int    prn       I   satellite prn/slot number
* return : satellite number (0:error)
*-----------------------------------------------------------------------------*/
extern int satno(int sys, int prn)
{
    if (prn<=0) return 0;
    switch (sys) {
        case SYS_GPS:
            if (prn<MINPRNGPS||MAXPRNGPS<prn) return 0;
            return prn-MINPRNGPS+1;
        case SYS_GLO:
            if (prn<MINPRNGLO||MAXPRNGLO<prn) return 0;
            return NSATGPS+prn-MINPRNGLO+1;
        case SYS_GAL:
            if (prn<MINPRNGAL||MAXPRNGAL<prn) return 0;
            return NSATGPS+NSATGLO+prn-MINPRNGAL+1;
        case SYS_QZS:
            if (prn<MINPRNQZS||MAXPRNQZS<prn) return 0;
            return NSATGPS+NSATGLO+NSATGAL+prn-MINPRNQZS+1;
        case SYS_CMP:
            if (prn<MINPRNCMP||MAXPRNCMP<prn) return 0;
            return NSATGPS+NSATGLO+NSATGAL+NSATQZS+prn-MINPRNCMP+1;
        case SYS_IRN:
            if (prn<MINPRNIRN||MAXPRNIRN<prn) return 0;
            return NSATGPS+NSATGLO+NSATGAL+NSATQZS+NSATCMP+prn-MINPRNIRN+1;
        case SYS_LEO:
            if (prn<MINPRNLEO||MAXPRNLEO<prn) return 0;
            return NSATGPS+NSATGLO+NSATGAL+NSATQZS+NSATCMP+NSATIRN+
                   prn-MINPRNLEO+1;
        case SYS_SBS:
            if (prn<MINPRNSBS||MAXPRNSBS<prn) return 0;
            return NSATGPS+NSATGLO+NSATGAL+NSATQZS+NSATCMP+NSATIRN+NSATLEO+
                   prn-MINPRNSBS+1;
    }
    return 0;
}
/* satellite number to satellite system ----------------------------------------
* convert satellite number to satellite system
* args   : int    sat       I   satellite number (1-MAXSAT)
*          int    *prn      IO  satellite prn/slot number (NULL: no output)
* return : satellite system (SYS_GPS,SYS_GLO,...)
*-----------------------------------------------------------------------------*/
extern int satsys(int sat, int *prn)
{
    int sys=SYS_NONE;
    if (sat<=0||MAXSAT<sat) sat=0;
    else if (sat<=NSATGPS) {
        sys=SYS_GPS; sat+=MINPRNGPS-1;
    }
    else if ((sat-=NSATGPS)<=NSATGLO) {
        sys=SYS_GLO; sat+=MINPRNGLO-1;
    }
    else if ((sat-=NSATGLO)<=NSATGAL) {
        sys=SYS_GAL; sat+=MINPRNGAL-1;
    }
    else if ((sat-=NSATGAL)<=NSATQZS) {
        sys=SYS_QZS; sat+=MINPRNQZS-1; 
    }
    else if ((sat-=NSATQZS)<=NSATCMP) {
        sys=SYS_CMP; sat+=MINPRNCMP-1; 
    }
    else if ((sat-=NSATCMP)<=NSATIRN) {
        sys=SYS_IRN; sat+=MINPRNIRN-1; 
    }
    else if ((sat-=NSATIRN)<=NSATLEO) {
        sys=SYS_LEO; sat+=MINPRNLEO-1; 
    }
    else if ((sat-=NSATLEO)<=NSATSBS) {
        sys=SYS_SBS; sat+=MINPRNSBS-1; 
    }
    else sat=0;
    if (prn) *prn=sat;
    return sys;
}
/* satellite id to satellite number --------------------------------------------
* convert satellite id to satellite number
* args   : char   *id       I   satellite id (nn,Gnn,Rnn,Enn,Jnn,Cnn,Inn or Snn)
* return : satellite number (0: error)
* notes  : 120-142 and 193-199 are also recognized as sbas and qzss
*-----------------------------------------------------------------------------*/
extern int satid2no(const char *id)
{
    int sys,prn;
    char code;
    
    if (sscanf(id,"%d",&prn)==1) {
        if      (MINPRNGPS<=prn&&prn<=MAXPRNGPS) sys=SYS_GPS;
        else if (MINPRNSBS<=prn&&prn<=MAXPRNSBS) sys=SYS_SBS;
        else if (MINPRNQZS<=prn&&prn<=MAXPRNQZS) sys=SYS_QZS;
        else return 0;
        return satno(sys,prn);
    }
    if (sscanf(id,"%c%d",&code,&prn)<2) return 0;
    
    switch (code) {
        case 'G': sys=SYS_GPS; prn+=MINPRNGPS-1; break;
        case 'R': sys=SYS_GLO; prn+=MINPRNGLO-1; break;
        case 'E': sys=SYS_GAL; prn+=MINPRNGAL-1; break;
        case 'J': sys=SYS_QZS; prn+=MINPRNQZS-1; break;
        case 'C': sys=SYS_CMP; prn+=MINPRNCMP-1; break;
        case 'I': sys=SYS_IRN; prn+=MINPRNIRN-1; break;
        case 'L': sys=SYS_LEO; prn+=MINPRNLEO-1; break;
        case 'S': sys=SYS_SBS; prn+=100; break;
        default: return 0;
    }
    return satno(sys,prn);
}
/* satellite number to satellite id --------------------------------------------
* convert satellite number to satellite id
* args   : int    sat       I   satellite number
*          char   *id       O   satellite id (Gnn,Rnn,Enn,Jnn,Cnn,Inn or nnn)
* return : none
*-----------------------------------------------------------------------------*/
extern void satno2id(int sat, char *id)
{
    int prn;
    switch (satsys(sat,&prn)) {
        case SYS_GPS: sprintf(id,"G%02d",prn-MINPRNGPS+1); return;
        case SYS_GLO: sprintf(id,"R%02d",prn-MINPRNGLO+1); return;
        case SYS_GAL: sprintf(id,"E%02d",prn-MINPRNGAL+1); return;
        case SYS_QZS: sprintf(id,"J%02d",prn-MINPRNQZS+1); return;
        case SYS_CMP: sprintf(id,"C%02d",prn-MINPRNCMP+1); return;
        case SYS_IRN: sprintf(id,"I%02d",prn-MINPRNIRN+1); return;
        case SYS_LEO: sprintf(id,"L%02d",prn-MINPRNLEO+1); return;
        case SYS_SBS: sprintf(id,"%03d" ,prn); return;
    }
    strcpy(id,"");
}
/* test excluded satellite -----------------------------------------------------
* test excluded satellite
* args   : int    sat       I   satellite number
*          int    svh       I   sv health flag
*          prcopt_t *opt    I   processing options (NULL: not used)
* return : status (1:excluded,0:not excluded)
*-----------------------------------------------------------------------------*/
extern int satexclude(int sat, int svh, const prcopt_t *opt)
{
    int sys=satsys(sat,NULL);
    
    if (svh<0) return 1; /* ephemeris unavailable */
    
    if (opt) {
        if (opt->exsats[sat-1]==1) return 1; /* excluded satellite */
        if (opt->exsats[sat-1]==2) return 0; /* included satellite */
        if (!(sys&opt->navsys)) return 1; /* unselected sat sys */
    }
    if (sys==SYS_QZS) svh&=0xFE; /* mask QZSS LEX health */
    if (svh) {
        arc_log(ARC_WARNING, "unhealthy satellite: sat=%3d svh=%02X\n",sat,svh);
        return 1;
    }
    return 0;
}
/* test SNR mask ---------------------------------------------------------------
* test SNR mask
* args   : int    base      I   rover or base-station (0:rover,1:base station)
*          int    freq      I   frequency (0:L1,1:L2,2:L3,...)
*          double el        I   elevation angle (rad)
*          double snr       I   C/N0 (dBHz)
*          snrmask_t *mask  I   SNR mask
* return : status (1:masked,0:unmasked)
*-----------------------------------------------------------------------------*/
extern int testsnr(int base, int freq, double el, double snr,
                   const snrmask_t *mask)
{
    double minsnr,a;
    int i;
    
    if (!mask->ena[base]||freq<0||freq>=NFREQ) return 0;
    
    a=(el*R2D+5.0)/10.0;
    i=(int)floor(a); a-=i;
    if      (i<1) minsnr=mask->mask[freq][0];
    else if (i>8) minsnr=mask->mask[freq][8];
    else minsnr=(1.0-a)*mask->mask[freq][i-1]+a*mask->mask[freq][i];
    
    return snr<minsnr;
}
/* obs type string to obs code -------------------------------------------------
* convert obs code type string to obs code
* args   : char   *str   I      obs code string ("1C","1P","1Y",...)
*          int    *freq  IO     frequency (1:L1,2:L2,3:L5,4:L6,5:L7,6:L8,0:err)
*                               (NULL: no output)
* return : obs code (CODE_???)
* notes  : obs codes are based on reference [6] and qzss extension
*-----------------------------------------------------------------------------*/
extern unsigned char obs2code(const char *obs, int *freq)
{
    int i;
    if (freq) *freq=0;
    for (i=1;*obscodes[i];i++) {
        if (strcmp(obscodes[i],obs)) continue;
        if (freq) *freq=obsfreqs[i];
        return (unsigned char)i;
    }
    return CODE_NONE;
}
/* obs code to obs code string -------------------------------------------------
* convert obs code to obs code string
* args   : unsigned char code I obs code (CODE_???)
*          int    *freq  IO     frequency (NULL: no output)
*                               (1:L1/E1, 2:L2/B1, 3:L5/E5a/L3, 4:L6/LEX/B3,
                                 5:E5b/B2, 6:E5(a+b), 7:S)
* return : obs code string ("1C","1P","1P",...)
* notes  : obs codes are based on reference [6] and qzss extension
*-----------------------------------------------------------------------------*/
extern char *code2obs(unsigned char code, int *freq)
{
    if (freq) *freq=0;
    if (code<=CODE_NONE||MAXCODE<code) return "";
    if (freq) *freq=obsfreqs[code];
    return obscodes[code];
}
/* set code priority -----------------------------------------------------------
* set code priority for multiple codes in a frequency
* args   : int    sys     I     system (or of SYS_???)
*          int    freq    I     frequency (1:L1,2:L2,3:L5,4:L6,5:L7,6:L8,7:L9)
*          char   *pri    I     priority of codes (series of code characters)
*                               (higher priority precedes lower)
* return : none
*-----------------------------------------------------------------------------*/
extern void setcodepri(int sys, int freq, const char *pri)
{
    arc_log(3, "setcodepri:sys=%d freq=%d pri=%s\n",sys,freq,pri);
    
    if (freq<=0||MAXFREQ<freq) return;
    if (sys&SYS_GPS) strcpy(codepris[0][freq-1],pri);
    if (sys&SYS_GLO) strcpy(codepris[1][freq-1],pri);
    if (sys&SYS_GAL) strcpy(codepris[2][freq-1],pri);
    if (sys&SYS_QZS) strcpy(codepris[3][freq-1],pri);
    if (sys&SYS_SBS) strcpy(codepris[4][freq-1],pri);
    if (sys&SYS_CMP) strcpy(codepris[5][freq-1],pri);
    if (sys&SYS_IRN) strcpy(codepris[6][freq-1],pri);
}
/* get code priority -----------------------------------------------------------
* get code priority for multiple codes in a frequency
* args   : int    sys     I     system (SYS_???)
*          unsigned char code I obs code (CODE_???)
*          char   *opt    I     code options (NULL:no option)
* return : priority (15:highest-1:lowest,0:error)
*-----------------------------------------------------------------------------*/
extern int getcodepri(int sys, unsigned char code, const char *opt)
{
    const char *p,*optstr;
    char *obs,str[8]="";
    int i,j;
    
    switch (sys) {
        case SYS_GPS: i=0; optstr="-GL%2s"; break;
        case SYS_GLO: i=1; optstr="-RL%2s"; break;
        case SYS_GAL: i=2; optstr="-EL%2s"; break;
        case SYS_QZS: i=3; optstr="-JL%2s"; break;
        case SYS_SBS: i=4; optstr="-SL%2s"; break;
        case SYS_CMP: i=5; optstr="-CL%2s"; break;
        case SYS_IRN: i=6; optstr="-IL%2s"; break;
        default: return 0;
    }
    obs=code2obs(code,&j);
    
    /* parse code options */
    for (p=opt;p&&(p=strchr(p,'-'));p++) {
        if (sscanf(p,optstr,str)<1||str[0]!=obs[0]) continue;
        return str[1]==obs[1]?15:0;
    }
    /* search code priority */
    return (p=strchr(codepris[i][j-1],obs[1]))?14-(int)(p-codepris[i][j-1]):0;
}
/* extract unsigned/signed bits ------------------------------------------------
* extract unsigned/signed bits from byte data
* args   : unsigned char *buff I byte data
*          int    pos    I      bit position from start of data (bits)
*          int    len    I      bit length (bits) (len<=32)
* return : extracted unsigned/signed bits
*-----------------------------------------------------------------------------*/
extern unsigned int getbitu(const unsigned char *buff, int pos, int len)
{
    unsigned int bits=0;
    int i;
    for (i=pos;i<pos+len;i++) bits=(bits<<1)+((buff[i/8]>>(7-i%8))&1u);
    return bits;
}
extern int getbits(const unsigned char *buff, int pos, int len)
{
    unsigned int bits=getbitu(buff,pos,len);
    if (len<=0||32<=len||!(bits&(1u<<(len-1)))) return (int)bits;
    return (int)(bits|(~0u<<len)); /* extend sign */
}
/* set unsigned/signed bits ----------------------------------------------------
* set unsigned/signed bits to byte data
* args   : unsigned char *buff IO byte data
*          int    pos    I      bit position from start of data (bits)
*          int    len    I      bit length (bits) (len<=32)
*         (unsigned) int I      unsigned/signed data
* return : none
*-----------------------------------------------------------------------------*/
extern void setbitu(unsigned char *buff, int pos, int len, unsigned int data)
{
    unsigned int mask=1u<<(len-1);
    int i;
    if (len<=0||32<len) return;
    for (i=pos;i<pos+len;i++,mask>>=1) {
        if (data&mask) buff[i/8]|=1u<<(7-i%8); else buff[i/8]&=~(1u<<(7-i%8));
    }
}
extern void setbits(unsigned char *buff, int pos, int len, int data)
{
    if (data<0) data|=1<<(len-1); else data&=~(1<<(len-1)); /* set sign bit */
    setbitu(buff,pos,len,(unsigned int)data);
}
/* new matrix ------------------------------------------------------------------
* allocate memory of matrix 
* args   : int    n,m       I   number of rows and columns of matrix
* return : matrix pointer (if n<=0 or m<=0, return NULL)
*-----------------------------------------------------------------------------*/
extern double *arc_mat(int n, int m)
{
    double *p;
    
    if (n<=0||m<=0) return NULL;
    if (!(p=(double *)malloc(sizeof(double)*n*m))) {
        fatalerr("matrix memory allocation error: n=%d,m=%d\n",n,m);
    }
    return p;
}
/* new integer matrix ----------------------------------------------------------
* allocate memory of integer matrix 
* args   : int    n,m       I   number of rows and columns of matrix
* return : matrix pointer (if n<=0 or m<=0, return NULL)
*-----------------------------------------------------------------------------*/
extern int *arc_imat(int n, int m)
{
    int *p;
    
    if (n<=0||m<=0) return NULL;
    if (!(p=(int *)malloc(sizeof(int)*n*m))) {
        fatalerr("integer matrix memory allocation error: n=%d,m=%d\n",n,m);
    }
    return p;
}
/* zero matrix -----------------------------------------------------------------
* generate new zero matrix
* args   : int    n,m       I   number of rows and columns of matrix
* return : matrix pointer (if n<=0 or m<=0, return NULL)
*-----------------------------------------------------------------------------*/
extern double *arc_zeros(int n, int m)
{
    double *p;
    
#if NOCALLOC
    if ((p=arc_mat(n,m))) for (n=n*m-1;n>=0;n--) p[n]=0.0;
#else
    if (n<=0||m<=0) return NULL;
    if (!(p=(double *)calloc(sizeof(double),n*m))) {
        fatalerr("matrix memory allocation error: n=%d,m=%d\n",n,m);
    }
#endif
    return p;
}
/* identity matrix -------------------------------------------------------------
* generate new identity matrix
* args   : int    n         I   number of rows and columns of matrix
* return : matrix pointer (if n<=0, return NULL)
*-----------------------------------------------------------------------------*/
extern double *arc_eye(int n)
{
    double *p;
    int i;
    
    if ((p= arc_zeros(n, n))) for (i=0;i<n;i++) p[i+i*n]=1.0;
    return p;
}
/* inner product ---------------------------------------------------------------
* inner product of vectors
* args   : double *a,*b     I   vector a,b (n x 1)
*          int    n         I   size of vector a,b
* return : a'*b
*-----------------------------------------------------------------------------*/
extern double arc_dot(const double *a, const double *b, int n)
{
    double c=0.0;
    
    while (--n>=0) c+=a[n]*b[n];
    return c;
}
/* euclid norm -----------------------------------------------------------------
* euclid norm of vector
* args   : double *a        I   vector a (n x 1)
*          int    n         I   size of vector a
* return : || a ||
*-----------------------------------------------------------------------------*/
extern double arc_norm(const double *a, int n)
{
    return sqrt(arc_dot(a, a, n));
}
/* outer product of 3d vectors -------------------------------------------------
* outer product of 3d vectors 
* args   : double *a,*b     I   vector a,b (3 x 1)
*          double *c        O   outer product (a x b) (3 x 1)
* return : none
*-----------------------------------------------------------------------------*/
extern void arc_cross3(const double *a, const double *b, double *c)
{
    c[0]=a[1]*b[2]-a[2]*b[1];
    c[1]=a[2]*b[0]-a[0]*b[2];
    c[2]=a[0]*b[1]-a[1]*b[0];
}
/* normalize 3d vector ---------------------------------------------------------
* normalize 3d vector
* args   : double *a        I   vector a (3 x 1)
*          double *b        O   normlized vector (3 x 1) || b || = 1
* return : status (1:ok,0:error)
*-----------------------------------------------------------------------------*/
extern int arc_normv3(const double *a, double *b)
{
    double r;
    if ((r= arc_norm(a, 3))<=0.0) return 0;
    b[0]=a[0]/r;
    b[1]=a[1]/r;
    b[2]=a[2]/r;
    return 1;
}
/* copy matrix -----------------------------------------------------------------
* copy matrix
* args   : double *A        O   destination matrix A (n x m)
*          double *B        I   source matrix B (n x m)
*          int    n,m       I   number of rows and columns of matrix
* return : none
*-----------------------------------------------------------------------------*/
extern void arc_matcpy(double *A, const double *B, int n, int m)
{
    memcpy(A,B,sizeof(double)*n*m);
}

/* multiply matrix -----------------------------------------------------------*/
extern void arc_matmul(const char *tr, int n, int k, int m, double alpha,
                       const double *A, const double *B, double beta, double *C)
{
    double d;
    int i,j,x,f=tr[0]=='N'?(tr[1]=='N'?1:2):(tr[1]=='N'?3:4);
    
    for (i=0;i<n;i++) for (j=0;j<k;j++) {
        d=0.0;
        switch (f) {
            case 1: for (x=0;x<m;x++) d+=A[i+x*n]*B[x+j*m]; break;
            case 2: for (x=0;x<m;x++) d+=A[i+x*n]*B[j+x*k]; break;
            case 3: for (x=0;x<m;x++) d+=A[x+i*m]*B[x+j*m]; break;
            case 4: for (x=0;x<m;x++) d+=A[x+i*m]*B[j+x*k]; break;
        }
        if (beta==0.0) C[i+j*n]=alpha*d; else C[i+j*n]=alpha*d+beta*C[i+j*n];
    }
}
/* LU decomposition ----------------------------------------------------------*/
static int arc_ludcmp(double *A, int n, int *indx, double *d)
{
    double big,s,tmp,*vv= arc_mat(n, 1);
    int i,imax=0,j,k;
    
    *d=1.0;
    for (i=0;i<n;i++) {
        big=0.0; for (j=0;j<n;j++) if ((tmp=fabs(A[i+j*n]))>big) big=tmp;
        if (big>0.0) vv[i]=1.0/big; else {free(vv); return -1;}
    }
    for (j=0;j<n;j++) {
        for (i=0;i<j;i++) {
            s=A[i+j*n]; for (k=0;k<i;k++) s-=A[i+k*n]*A[k+j*n]; A[i+j*n]=s;
        }
        big=0.0;
        for (i=j;i<n;i++) {
            s=A[i+j*n]; for (k=0;k<j;k++) s-=A[i+k*n]*A[k+j*n]; A[i+j*n]=s;
            if ((tmp=vv[i]*fabs(s))>=big) {big=tmp; imax=i;}
        }
        if (j!=imax) {
            for (k=0;k<n;k++) {
                tmp=A[imax+k*n]; A[imax+k*n]=A[j+k*n]; A[j+k*n]=tmp;
            }
            *d=-(*d); vv[imax]=vv[j];
        }
        indx[j]=imax;
        if (A[j+j*n]==0.0) {free(vv); return -1;}
        if (j!=n-1) {
            tmp=1.0/A[j+j*n]; for (i=j+1;i<n;i++) A[i+j*n]*=tmp;
        }
    }
    free(vv);
    return 0;
}
/* LU back-substitution ------------------------------------------------------*/
static void arc_lubksb(const double *A, int n, const int *indx, double *b)
{
    double s;
    int i,ii=-1,ip,j;
    
    for (i=0;i<n;i++) {
        ip=indx[i]; s=b[ip]; b[ip]=b[i];
        if (ii>=0) for (j=ii;j<i;j++) s-=A[i+j*n]*b[j]; else if (s) ii=i;
        b[i]=s;
    }
    for (i=n-1;i>=0;i--) {
        s=b[i]; for (j=i+1;j<n;j++) s-=A[i+j*n]*b[j]; b[i]=s/A[i+i*n];
    }
}
/* inverse of matrix ---------------------------------------------------------*/
extern int arc_matinv(double *A, int n)
{
    double d,*B;
    int i,j,*indx;
    
    indx= arc_imat(n, 1); B= arc_mat(n, n);
    arc_matcpy(B, A, n, n);
    if (arc_ludcmp(B,n,indx,&d)) {free(indx); free(B); return -1;}
    for (j=0;j<n;j++) {
        for (i=0;i<n;i++) A[i+j*n]=0.0; A[j+j*n]=1.0;
        arc_lubksb(B,n,indx,A+j*n);
    }
    free(indx); free(B);
    return 0;
}
/* solve linear equation -----------------------------------------------------*/
extern int arc_solve(const char *tr, const double *A, const double *Y, int n,
                     int m, double *X)
{
    double *B=arc_mat(n,n);
    int info;

    arc_matcpy(B,A,n,n);
    if (!(info= arc_matinv(B,n))) arc_matmul(tr[0]=='N'?"NN":"TN",n,m,n,1.0,B,Y,0.0,X);
    free(B);
    return info;
}
/* arc cholesky functions ------------------------------------------------------*/
extern double *arc_cholesky(double *A,int n)
{
    int i,j,k;
    double *L=(double*)calloc(n*n,sizeof(double));
    if (L==NULL)
        fprintf(stderr,"Falta cholesky decomp \n");
    for (i=0;i<n;i++) {
        for (j=0;j<(i+1);j++) {
            double s=0;
            for (k=0;k<j;k++) {
                s+= L[i *n+k]*L[j*n+k];
            }
            if (i==j) {
                L[i*n+j]=sqrt(A[i*n+i]-s);
            }
            else {
                L[i*n+j]=(1.0/L[j*n+j]*(A[i*n+j]-s));
            }
        }
    }
    return L;
}
/* end of matrix routines ----------------------------------------------------*/

/* least square estimation -----------------------------------------------------
* least square estimation by solving normal equation (x=(A*A')^-1*A*y)
* args   : double *A        I   transpose of (weighted) design matrix (n x m)
*          double *y        I   (weighted) measurements (m x 1)
*          int    n,m       I   number of parameters and measurements (n<=m)
*          double *x        O   estmated parameters (n x 1)
*          double *Q        O   esimated parameters covariance matrix (n x n)
* return : status (0:ok,0>:error)
* notes  : for weighted least square, replace A and y by A*w and w*y (w=W^(1/2))
*          matirix stored by column-major order (fortran convention)
*-----------------------------------------------------------------------------*/
extern int arc_lsq(const double *A, const double *y, int n, int m, double *x,
                   double *Q)
{
    double *Ay;
    int info;
    
    if (m<n) return -1;
    Ay= arc_mat(n, 1);
    arc_matmul("NN", n, 1, m, 1.0, A, y, 0.0, Ay); /* Ay=A*y */
    arc_matmul("NT", n, n, m, 1.0, A, A, 0.0, Q);  /* Q=A*A' */
    if (!(info= arc_matinv(Q, n))) arc_matmul("NN", n, 1, n, 1.0, Q, Ay, 0.0, x); /* x=Q^-1*Ay */
    free(Ay);
    return info;
}
/* kalman filter ---------------------------------------------------------------
* kalman filter state update as follows:
*
*   K=P*H*(H'*P*H+R)^-1, xp=x+K*v, Pp=(I-K*H')*P
*
* args   : double *x        I   states vector (n x 1)
*          double *P        I   covariance matrix of states (n x n)
*          double *H        I   transpose of design matrix (n x m)
*          double *v        I   innovation (measurement - model) (m x 1)
*          double *R        I   covariance matrix of measurement error (m x m)
*          int    n,m       I   number of states and measurements
*          double *xp       O   states vector after update (n x 1)
*          double *Pp       O   covariance matrix of states after update (n x n)
* return : status (0:ok,<0:error)
* notes  : matirix stored by column-major order (fortran convention)
*          if state x[i]==0.0, not updates state x[i]/P[i+i*n]
*-----------------------------------------------------------------------------*/
static int arc_filter_(const double *x, const double *P, const double *H,
                       const double *v, const double *R, int n, int m,
                       double *xp, double *Pp,double *D)
{
    double *F=arc_mat(n,m),*Q= arc_mat(m,m),*K=arc_mat(n,m),*I=arc_eye(n),
           *KK=arc_mat(n,m);
    int info;

    arc_matcpy(Q,R,m,m);
    arc_matcpy(xp,x,n,1);
    arc_matmul("NN",n,m,n,1.0,P,H,0.0,F);       /* Q=H'*P*H+R */
    arc_matmul("TN",m,m,n,1.0,H,F,1.0,Q);
    if (!(info=arc_matinv(Q,m))) {
        arc_matmul("NN",n,m,m,1.0,F,Q,0.0,K);   /* K=P*H*Q^-1 */
        arc_matmul("NN",n,m,m,1.0,K,D,0.0,KK);  /* robust */
        arc_matmul("NN",n,1,m,1.0,KK,v,1.0,xp); /* xp=x+K*v */
        arc_matmul("NT",n,n,m,-1.0,K,H,1.0,I);  /* Pp=(I-K*H')*P */
        arc_matmul("NN",n,n,n,1.0,I,P,0.0,Pp);
    }
    free(F); free(Q); free(K); free(I); free(KK);
    return info;
}
extern int arc_filter(double *x, double *P, const double *H, const double *v,
                      const double *R, int n, int m,double *D)
{
    double *x_,*xp_,*P_,*Pp_,*H_,*D_=arc_eye(m);
    int i,j,k,info,*ix;

    if (D) arc_matcpy(D_,D,m,m);
    
    ix=arc_imat(n,1); for (i=k=0;i<n;i++) if (x[i]!=0.0&&P[i+i*n]>0.0) ix[k++]=i;
    x_=arc_mat(k,1); xp_=arc_mat(k,1); P_=arc_mat(k,k); Pp_=arc_mat(k,k); H_=arc_mat(k,m);
    for (i=0;i<k;i++) {
        x_[i]=x[ix[i]];
        for (j=0;j<k;j++) P_[i+j*k]=P[ix[i]+ix[j]*n];
        for (j=0;j<m;j++) H_[i+j*k]=H[ix[i]+j*n];
    }
    info=arc_filter_(x_,P_,H_,v,R,k,m,xp_,Pp_,D_);
    for (i=0;i<k;i++) {
        x[ix[i]]=xp_[i];
        for (j=0;j<k;j++) P[ix[i]+ix[j]*n]=Pp_[i+j*k];
    }
    free(ix); free(x_); free(xp_); free(P_); free(Pp_); free(H_); free(D_);
    return info;
}
/* smoother --------------------------------------------------------------------
* combine forward and backward filters by fixed-interval smoother as follows:
*
*   xs=Qs*(Qf^-1*xf+Qb^-1*xb), Qs=(Qf^-1+Qb^-1)^-1)
*
* args   : double *xf       I   forward solutions (n x 1)
* args   : double *Qf       I   forward solutions covariance matrix (n x n)
*          double *xb       I   backward solutions (n x 1)
*          double *Qb       I   backward solutions covariance matrix (n x n)
*          int    n         I   number of solutions
*          double *xs       O   smoothed solutions (n x 1)
*          double *Qs       O   smoothed solutions covariance matrix (n x n)
* return : status (0:ok,0>:error)
* notes  : see reference [4] 5.2
*          matirix stored by column-major order (fortran convention)
*-----------------------------------------------------------------------------*/
extern int arc_smoother(const double *xf, const double *Qf, const double *xb,
                        const double *Qb, int n, double *xs, double *Qs)
{
    double *invQf= arc_mat(n, n),*invQb= arc_mat(n, n),*xx= arc_mat(n, 1);
    int i,info=-1;

    arc_matcpy(invQf, Qf, n, n);
    arc_matcpy(invQb, Qb, n, n);
    if (!arc_matinv(invQf, n)&&!arc_matinv(invQb, n)) {
        for (i=0;i<n*n;i++) Qs[i]=invQf[i]+invQb[i];
        if (!(info= arc_matinv(Qs, n))) {
            arc_matmul("NN", n, 1, n, 1.0, invQf, xf, 0.0, xx);
            arc_matmul("NN", n, 1, n, 1.0, invQb, xb, 1.0, xx);
            arc_matmul("NN", n, 1, n, 1.0, Qs, xx, 0.0, xs);
        }
    }
    free(invQf); free(invQb); free(xx);
    return info;
}
/* print matrix ----------------------------------------------------------------
* print matrix to stdout
* args   : double *A        I   matrix A (n x m)
*          int    n,m       I   number of rows and columns of A
*          int    p,q       I   total columns, columns under decimal point
*         (FILE  *fp        I   output file pointer)
* return : none
* notes  : matirix stored by column-major order (fortran convention)
*-----------------------------------------------------------------------------*/
extern void matfprint(const double A[], int n, int m, int p, int q, FILE *fp)
{
    int i,j;
    static char *str="-";
    for (i=0;i<p*m+5;i++) fprintf(fp,str);fprintf(fp,"\n");
    for (i=0;i<n;i++) {
        for (j=0;j<m;j++) fprintf(fp," %*.*f",p,q,A[i+j*n]);
        fprintf(fp,"\n");
    }
}
/* print matrix ----------------------------------------------------------------
* print matrix to stdout
* args   : int    *A        I   matrix A (n x m)
*          int    n,m       I   number of rows and columns of A
*          int    p,q       I   total columns, columns under decimal point
*         (FILE  *fp        I   output file pointer)
* return : none
* notes  : matirix stored by column-major order (fortran convention)
*-----------------------------------------------------------------------------*/
extern void matfprinti(const int A[], int n, int m, int p, int q, FILE *fp)
{
    int i,j;
    fprintf(fp,"------------------------------------------------------------\n");
    for (i=0;i<n;i++) {
        for (j=0;j<m;j++) fprintf(fp," %*.*d",p,q,A[i+j*n]);
        fprintf(fp,"\n");
    }
}
extern void matprint(const double A[], int n, int m, int p, int q)
{
    matfprint(A,n,m,p,q,stdout);
}
/* string to number ------------------------------------------------------------
* convert substring in string to number
* args   : char   *s        I   string ("... nnn.nnn ...")
*          int    i,n       I   substring position and width
* return : converted number (0.0:error)
*-----------------------------------------------------------------------------*/
extern double str2num(const char *s, int i, int n)
{
    double value;
    char str[256],*p=str;
    
    if (i<0||(int)strlen(s)<i||(int)sizeof(str)-1<n) return 0.0;
    for (s+=i;*s&&--n>=0;s++) *p++=*s=='d'||*s=='D'?'E':*s; *p='\0';
    return sscanf(str,"%lf",&value)==1?value:0.0;
}
/* string to time --------------------------------------------------------------
* convert substring in string to gtime_t struct
* args   : char   *s        I   string ("... yyyy mm dd hh mm ss ...")
*          int    i,n       I   substring position and width
*          gtime_t *t       O   gtime_t struct
* return : status (0:ok,0>:error)
*-----------------------------------------------------------------------------*/
extern int str2time(const char *s, int i, int n, gtime_t *t)
{
    double ep[6];
    char str[256],*p=str;
    
    if (i<0||(int)strlen(s)<i||(int)sizeof(str)-1<i) return -1;
    for (s+=i;*s&&--n>=0;) *p++=*s++; *p='\0';
    if (sscanf(str,"%lf %lf %lf %lf %lf %lf",ep,ep+1,ep+2,ep+3,ep+4,ep+5)<6)
        return -1;
    if (ep[0]<100.0) ep[0]+=ep[0]<80.0?2000.0:1900.0;
    *t=epoch2time(ep);
    return 0;
}
/* convert calendar day/time to time -------------------------------------------
* convert calendar day/time to gtime_t struct
* args   : double *ep       I   day/time {year,month,day,hour,min,sec}
* return : gtime_t struct
* notes  : proper in 1970-2037 or 1970-2099 (64bit time_t)
*-----------------------------------------------------------------------------*/
extern gtime_t epoch2time(const double *ep)
{
    const int doy[]={1,32,60,91,121,152,182,213,244,274,305,335};
    gtime_t time={0};
    int days,sec,year=(int)ep[0],mon=(int)ep[1],day=(int)ep[2];
    
    if (year<1970||2099<year||mon<1||12<mon) return time;
    
    /* leap year if year%4==0 in 1901-2099 */
    days=(year-1970)*365+(year-1969)/4+doy[mon-1]+day-2+(year%4==0&&mon>=3?1:0);
    sec=(int)floor(ep[5]);
    time.time=(time_t)days*86400+(int)ep[3]*3600+(int)ep[4]*60+sec;
    time.sec=ep[5]-sec;
    return time;
}
/* time to calendar day/time ---------------------------------------------------
* convert gtime_t struct to calendar day/time
* args   : gtime_t t        I   gtime_t struct
*          double *ep       O   day/time {year,month,day,hour,min,sec}
* return : none
* notes  : proper in 1970-2037 or 1970-2099 (64bit time_t)
*-----------------------------------------------------------------------------*/
extern void time2epoch(gtime_t t, double *ep)
{
    const int mday[]={ /* # of days in a month */
        31,28,31,30,31,30,31,31,30,31,30,31,31,28,31,30,31,30,31,31,30,31,30,31,
        31,29,31,30,31,30,31,31,30,31,30,31,31,28,31,30,31,30,31,31,30,31,30,31
    };
    int days,sec,mon,day;
    
    /* leap year if year%4==0 in 1901-2099 */
    days=(int)(t.time/86400);
    sec=(int)(t.time-(time_t)days*86400);
    for (day=days%1461,mon=0;mon<48;mon++) {
        if (day>=mday[mon]) day-=mday[mon]; else break;
    }
    ep[0]=1970+days/1461*4+mon/12; ep[1]=mon%12+1; ep[2]=day+1;
    ep[3]=sec/3600; ep[4]=sec%3600/60; ep[5]=sec%60+t.sec;
}
/* gps time to time ------------------------------------------------------------
* convert week and tow in gps time to gtime_t struct
* args   : int    week      I   week number in gps time
*          double sec       I   time of week in gps time (s)
* return : gtime_t struct
*-----------------------------------------------------------------------------*/
extern gtime_t gpst2time(int week, double sec)
{
    gtime_t t=epoch2time(gpst0);
    
    if (sec<-1E9||1E9<sec) sec=0.0;
    t.time+=(time_t)86400*7*week+(int)sec;
    t.sec=sec-(int)sec;
    return t;
}
/* time to gps time ------------------------------------------------------------
* convert gtime_t struct to week and tow in gps time
* args   : gtime_t t        I   gtime_t struct
*          int    *week     IO  week number in gps time (NULL: no output)
* return : time of week in gps time (s)
*-----------------------------------------------------------------------------*/
extern double time2gpst(gtime_t t, int *week)
{
    gtime_t t0=epoch2time(gpst0);
    time_t sec=t.time-t0.time;
    int w=(int)(sec/(86400*7));
    
    if (week) *week=w;
    return (double)(sec-(double)w*86400*7)+t.sec;
}
/* galileo system time to time -------------------------------------------------
* convert week and tow in galileo system time (gst) to gtime_t struct
* args   : int    week      I   week number in gst
*          double sec       I   time of week in gst (s)
* return : gtime_t struct
*-----------------------------------------------------------------------------*/
extern gtime_t gst2time(int week, double sec)
{
    gtime_t t=epoch2time(gst0);
    
    if (sec<-1E9||1E9<sec) sec=0.0;
    t.time+=(time_t)86400*7*week+(int)sec;
    t.sec=sec-(int)sec;
    return t;
}
/* time to galileo system time -------------------------------------------------
* convert gtime_t struct to week and tow in galileo system time (gst)
* args   : gtime_t t        I   gtime_t struct
*          int    *week     IO  week number in gst (NULL: no output)
* return : time of week in gst (s)
*-----------------------------------------------------------------------------*/
extern double time2gst(gtime_t t, int *week)
{
    gtime_t t0=epoch2time(gst0);
    time_t sec=t.time-t0.time;
    int w=(int)(sec/(86400*7));
    
    if (week) *week=w;
    return (double)(sec-(double)w*86400*7)+t.sec;
}
/* beidou time (bdt) to time ---------------------------------------------------
* convert week and tow in beidou time (bdt) to gtime_t struct
* args   : int    week      I   week number in bdt
*          double sec       I   time of week in bdt (s)
* return : gtime_t struct
*-----------------------------------------------------------------------------*/
extern gtime_t bdt2time(int week, double sec)
{
    gtime_t t=epoch2time(bdt0);
    
    if (sec<-1E9||1E9<sec) sec=0.0;
    t.time+=(time_t)86400*7*week+(int)sec;
    t.sec=sec-(int)sec;
    return t;
}
/* time to beidouo time (bdt) --------------------------------------------------
* convert gtime_t struct to week and tow in beidou time (bdt)
* args   : gtime_t t        I   gtime_t struct
*          int    *week     IO  week number in bdt (NULL: no output)
* return : time of week in bdt (s)
*-----------------------------------------------------------------------------*/
extern double time2bdt(gtime_t t, int *week)
{
    gtime_t t0=epoch2time(bdt0);
    time_t sec=t.time-t0.time;
    int w=(int)(sec/(86400*7));
    
    if (week) *week=w;
    return (double)(sec-(double)w*86400*7)+t.sec;
}
/* add time --------------------------------------------------------------------
* add time to gtime_t struct
* args   : gtime_t t        I   gtime_t struct
*          double sec       I   time to add (s)
* return : gtime_t struct (t+sec)
*-----------------------------------------------------------------------------*/
extern gtime_t timeadd(gtime_t t, double sec)
{
    double tt;
    
    t.sec+=sec; tt=floor(t.sec); t.time+=(int)tt; t.sec-=tt;
    return t;
}
/* time difference -------------------------------------------------------------
* difference between gtime_t structs
* args   : gtime_t t1,t2    I   gtime_t structs
* return : time difference (t1-t2) (s)
*-----------------------------------------------------------------------------*/
extern double timediff(gtime_t t1, gtime_t t2)
{
    return difftime(t1.time,t2.time)+t1.sec-t2.sec;
}
/* get current time in utc -----------------------------------------------------
* get current time in utc
* args   : none
* return : current time in utc
*-----------------------------------------------------------------------------*/
static double timeoffset_=0.0;        /* time offset (s) */

extern gtime_t timeget(void)
{
    gtime_t time;
    double ep[6]={0};
#ifdef WIN32
    SYSTEMTIME ts;
    
    GetSystemTime(&ts); /* utc */
    ep[0]=ts.wYear; ep[1]=ts.wMonth;  ep[2]=ts.wDay;
    ep[3]=ts.wHour; ep[4]=ts.wMinute; ep[5]=ts.wSecond+ts.wMilliseconds*1E-3;
#else
    struct timeval tv;
    struct tm *tt;
    
    if (!gettimeofday(&tv,NULL)&&(tt=gmtime(&tv.tv_sec))) {
        ep[0]=tt->tm_year+1900; ep[1]=tt->tm_mon+1; ep[2]=tt->tm_mday;
        ep[3]=tt->tm_hour; ep[4]=tt->tm_min; ep[5]=tt->tm_sec+tv.tv_usec*1E-6;
    }
#endif
    time=epoch2time(ep);
    
#ifdef CPUTIME_IN_GPST /* cputime operated in gpst */
    time=gpst2utc(time);
#endif
    return timeadd(time,timeoffset_);
}
/* set current time in utc -----------------------------------------------------
* set current time in utc
* args   : gtime_t          I   current time in utc
* return : none
* notes  : just set time offset between cpu time and current time
*          the time offset is reflected to only timeget()
*          not reentrant
*-----------------------------------------------------------------------------*/
extern void timeset(gtime_t t)
{
    timeoffset_+=timediff(t,timeget());
}
/* read leap seconds table by text -------------------------------------------*/
static int read_leaps_text(FILE *fp)
{
    char buff[256],*p;
    int i,n=0,ep[6],ls;
    
    rewind(fp);
    
    while (fgets(buff,sizeof(buff),fp)&&n<MAXLEAPS) {
        if ((p=strchr(buff,'#'))) *p='\0';
        if (sscanf(buff,"%d %d %d %d %d %d %d",ep,ep+1,ep+2,ep+3,ep+4,ep+5,
                   &ls)<7) continue;
        for (i=0;i<6;i++) leaps[n][i]=ep[i];
        leaps[n++][6]=ls;
    }
    return n;
}
/* read leap seconds table by usno -------------------------------------------*/
static int read_leaps_usno(FILE *fp)
{
    static const char *months[]={
        "JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"
    };
    double jd,tai_utc;
    char buff[256],month[32],ls[MAXLEAPS][7]={{0}};
    int i,j,y,m,d,n=0;
    
    rewind(fp);
    
    while (fgets(buff,sizeof(buff),fp)&&n<MAXLEAPS) {
        if (sscanf(buff,"%d %s %d =JD %lf TAI-UTC= %lf",&y,month,&d,&jd,
                   &tai_utc)<5) continue;
        if (y<1980) continue;
        for (m=1;m<=12;m++) if (!strcmp(months[m-1],month)) break;
        if (m>=13) continue;
        ls[n][0]=y;
        ls[n][1]=m;
        ls[n][2]=d;
        ls[n++][6]=(char)(19.0-tai_utc);
    }
    for (i=0;i<n;i++) for (j=0;j<7;j++) {
        leaps[i][j]=ls[n-i-1][j];
    }
    return n;
}
/* read leap seconds table -----------------------------------------------------
* read leap seconds table
* args   : char    *file    I   leap seconds table file
* return : status (1:ok,0:error)
* notes  : The leap second table should be as follows or leapsec.dat provided
*          by USNO.
*          (1) The records in the table file cosist of the following fields:
*              year month day hour min sec UTC-GPST(s)
*          (2) The date and time indicate the start UTC time for the UTC-GPST
*          (3) The date and time should be descending order.
*-----------------------------------------------------------------------------*/
extern int read_leaps(const char *file)
{
    FILE *fp;
    int i,n;
    
    if (!(fp=fopen(file,"r"))) return 0;
    
    /* read leap seconds table by text or usno */
    if (!(n=read_leaps_text(fp))&&!(n=read_leaps_usno(fp))) {
        fclose(fp);
        return 0;
    }
    for (i=0;i<7;i++) leaps[n][i]=0.0;
    fclose(fp);
    return 1;
}
/* gpstime to utc --------------------------------------------------------------
* convert gpstime to utc considering leap seconds
* args   : gtime_t t        I   time expressed in gpstime
* return : time expressed in utc
* notes  : ignore slight time offset under 100 ns
*-----------------------------------------------------------------------------*/
extern gtime_t gpst2utc(gtime_t t)
{
    gtime_t tu;
    int i;
    
    for (i=0;leaps[i][0]>0;i++) {
        tu=timeadd(t,leaps[i][6]);
        if (timediff(tu,epoch2time(leaps[i]))>=0.0) return tu;
    }
    return t;
}
/* utc to gpstime --------------------------------------------------------------
* convert utc to gpstime considering leap seconds
* args   : gtime_t t        I   time expressed in utc
* return : time expressed in gpstime
* notes  : ignore slight time offset under 100 ns
*-----------------------------------------------------------------------------*/
extern gtime_t utc2gpst(gtime_t t)
{
    int i;
    
    for (i=0;leaps[i][0]>0;i++) {
        if (timediff(t,epoch2time(leaps[i]))>=0.0) return timeadd(t,-leaps[i][6]);
    }
    return t;
}
/* gpstime to bdt --------------------------------------------------------------
* convert gpstime to bdt (beidou navigation satellite system time)
* args   : gtime_t t        I   time expressed in gpstime
* return : time expressed in bdt
* notes  : ref [8] 3.3, 2006/1/1 00:00 BDT = 2006/1/1 00:00 UTC
*          no leap seconds in BDT
*          ignore slight time offset under 100 ns
*-----------------------------------------------------------------------------*/
extern gtime_t gpst2bdt(gtime_t t)
{
    return timeadd(t,-14.0);
}
/* bdt to gpstime --------------------------------------------------------------
* convert bdt (beidou navigation satellite system time) to gpstime
* args   : gtime_t t        I   time expressed in bdt
* return : time expressed in gpstime
* notes  : see gpst2bdt()
*-----------------------------------------------------------------------------*/
extern gtime_t bdt2gpst(gtime_t t)
{
    return timeadd(t,14.0);
}
/* time to day and sec -------------------------------------------------------*/
static double time2sec(gtime_t time, gtime_t *day)
{
    double ep[6],sec;
    time2epoch(time,ep);
    sec=ep[3]*3600.0+ep[4]*60.0+ep[5];
    ep[3]=ep[4]=ep[5]=0.0;
    *day=epoch2time(ep);
    return sec;
}
/* utc to gmst -----------------------------------------------------------------
* convert utc to gmst (Greenwich mean sidereal time)
* args   : gtime_t t        I   time expressed in utc
*          double ut1_utc   I   UT1-UTC (s)
* return : gmst (rad)
*-----------------------------------------------------------------------------*/
extern double utc2gmst(gtime_t t, double ut1_utc)
{
    const double ep2000[]={2000,1,1,12,0,0};
    gtime_t tut,tut0;
    double ut,t1,t2,t3,gmst0,gmst;
    
    tut=timeadd(t,ut1_utc);
    ut=time2sec(tut,&tut0);
    t1=timediff(tut0,epoch2time(ep2000))/86400.0/36525.0;
    t2=t1*t1; t3=t2*t1;
    gmst0=24110.54841+8640184.812866*t1+0.093104*t2-6.2E-6*t3;
    gmst=gmst0+1.002737909350795*ut;
    
    return fmod(gmst,86400.0)*PI/43200.0; /* 0 <= gmst <= 2*PI */
}
/* time to string --------------------------------------------------------------
* convert gtime_t struct to string
* args   : gtime_t t        I   gtime_t struct
*          char   *s        O   string ("yyyy/mm/dd hh:mm:ss.ssss")
*          int    n         I   number of decimals
* return : none
*-----------------------------------------------------------------------------*/
extern void time2str(gtime_t t, char *s, int n)
{
    double ep[6];
    
    if (n<0) n=0; else if (n>12) n=12;
    if (1.0-t.sec<0.5/pow(10.0,n)) {t.time++; t.sec=0.0;};
    time2epoch(t,ep);
    sprintf(s,"%04.0f/%02.0f/%02.0f %02.0f:%02.0f:%0*.*f",ep[0],ep[1],ep[2],
            ep[3],ep[4],n<=0?2:n+3,n<=0?0:n,ep[5]);
}
/* get time string -------------------------------------------------------------
* get time string
* args   : gtime_t t        I   gtime_t struct
*          int    n         I   number of decimals
* return : time string
* notes  : not reentrant, do not use multiple in a function
*-----------------------------------------------------------------------------*/
extern char *time_str(gtime_t t, int n)
{
    static char buff[64];
    time2str(t,buff,n);
    return buff;
}
/* time to day of year ---------------------------------------------------------
* convert time to day of year
* args   : gtime_t t        I   gtime_t struct
* return : day of year (days)
*-----------------------------------------------------------------------------*/
extern double time2doy(gtime_t t)
{
    double ep[6];
    
    time2epoch(t,ep);
    ep[1]=ep[2]=1.0; ep[3]=ep[4]=ep[5]=0.0;
    return timediff(t,epoch2time(ep))/86400.0+1.0;
}
/* adjust gps week number ------------------------------------------------------
* adjust gps week number using cpu time
* args   : int   week       I   not-adjusted gps week number
* return : adjusted gps week number
*-----------------------------------------------------------------------------*/
extern int adjgpsweek(int week)
{
    int w;
    (void)time2gpst(utc2gpst(timeget()),&w);
    if (w<1560) w=1560; /* use 2009/12/1 if time is earlier than 2009/12/1 */
    return week+(w-week+512)/1024*1024;
}
/* sleep ms --------------------------------------------------------------------
* sleep ms
* args   : int   ms         I   miliseconds to sleep (<0:no sleep)
* return : none
*-----------------------------------------------------------------------------*/
extern void sleepms(int ms)
{
#ifdef WIN32
    if (ms<5) Sleep(1); else Sleep(ms);
#else
    struct timespec ts;
    if (ms<=0) return;
    ts.tv_sec=(time_t)(ms/1000);
    ts.tv_nsec=(long)(ms%1000*1000000);
    nanosleep(&ts,NULL);
#endif
}
/* convert degree to deg-min-sec -----------------------------------------------
* convert degree to degree-minute-second
* args   : double deg       I   degree
*          double *dms      O   degree-minute-second {deg,min,sec}
*          int    ndec      I   number of decimals of second
* return : none
*-----------------------------------------------------------------------------*/
extern void deg2dms(double deg, double *dms, int ndec)
{
    double sign=deg<0.0?-1.0:1.0,a=fabs(deg);
    double unit=pow(0.1,ndec);
    dms[0]=floor(a); a=(a-dms[0])*60.0;
    dms[1]=floor(a); a=(a-dms[1])*60.0;
    dms[2]=floor(a/unit+0.5)*unit;
    if (dms[2]>=60.0) {
        dms[2]=0.0;
        dms[1]+=1.0;
        if (dms[1]>=60.0) {
            dms[1]=0.0;
            dms[0]+=1.0;
        }
    }
    dms[0]*=sign;
}
/* convert deg-min-sec to degree -----------------------------------------------
* convert degree-minute-second to degree
* args   : double *dms      I   degree-minute-second {deg,min,sec}
* return : degree
*-----------------------------------------------------------------------------*/
extern double dms2deg(const double *dms)
{
    double sign=dms[0]<0.0?-1.0:1.0;
    return sign*(fabs(dms[0])+dms[1]/60.0+dms[2]/3600.0);
}
/* transform ecef to geodetic postion ------------------------------------------
* transform ecef position to geodetic position
* args   : double *r        I   ecef position {x,y,z} (m)
*          double *pos      O   geodetic position {lat,lon,h} (rad,m)
* return : none
* notes  : WGS84, ellipsoidal height
*-----------------------------------------------------------------------------*/
extern void ecef2pos(const double *r, double *pos)
{
    double e2=FE_WGS84*(2.0-FE_WGS84),r2= arc_dot(r, r, 2),z,zk,v=RE_WGS84,sinp;
    
    for (z=r[2],zk=0.0;fabs(z-zk)>=1E-4;) {
        zk=z;
        sinp=z/sqrt(r2+z*z);
        v=RE_WGS84/sqrt(1.0-e2*sinp*sinp);
        z=r[2]+v*e2*sinp;
    }
    pos[0]=r2>1E-12?atan(z/sqrt(r2)):(r[2]>0.0?PI/2.0:-PI/2.0);
    pos[1]=r2>1E-12?atan2(r[1],r[0]):0.0;
    pos[2]=sqrt(r2+z*z)-v;
}
/* transform geodetic to ecef position -----------------------------------------
* transform geodetic position to ecef position
* args   : double *pos      I   geodetic position {lat,lon,h} (rad,m)
*          double *r        O   ecef position {x,y,z} (m)
* return : none
* notes  : WGS84, ellipsoidal height
*-----------------------------------------------------------------------------*/
extern void pos2ecef(const double *pos, double *r)
{
    double sinp=sin(pos[0]),cosp=cos(pos[0]),sinl=sin(pos[1]),cosl=cos(pos[1]);
    double e2=FE_WGS84*(2.0-FE_WGS84),v=RE_WGS84/sqrt(1.0-e2*sinp*sinp);
    
    r[0]=(v+pos[2])*cosp*cosl;
    r[1]=(v+pos[2])*cosp*sinl;
    r[2]=(v*(1.0-e2)+pos[2])*sinp;
}
/* ecef to local coordinate transfromation matrix ------------------------------
* compute ecef to local coordinate transfromation matrix
* args   : double *pos      I   geodetic position {lat,lon} (rad)
*          double *E        O   ecef to local coord transformation matrix (3x3)
* return : none
* notes  : matirix stored by column-major order (fortran convention)
*-----------------------------------------------------------------------------*/
extern void xyz2enu(const double *pos, double *E)
{
    double sinp=sin(pos[0]),cosp=cos(pos[0]),sinl=sin(pos[1]),cosl=cos(pos[1]);
    
    E[0]=-sinl;      E[3]=cosl;       E[6]=0.0;
    E[1]=-sinp*cosl; E[4]=-sinp*sinl; E[7]=cosp;
    E[2]=cosp*cosl;  E[5]=cosp*sinl;  E[8]=sinp;
}
/* transform ecef vector to local tangental coordinate -------------------------
* transform ecef vector to local tangental coordinate
* args   : double *pos      I   geodetic position {lat,lon} (rad)
*          double *r        I   vector in ecef coordinate {x,y,z}
*          double *e        O   vector in local tangental coordinate {e,n,u}
* return : none
*-----------------------------------------------------------------------------*/
extern void ecef2enu(const double *pos, const double *r, double *e)
{
    double E[9];
    
    xyz2enu(pos,E);
    arc_matmul("NN", 3, 1, 3, 1.0, E, r, 0.0, e);
}
/* transform local vector to ecef coordinate -----------------------------------
* transform local tangental coordinate vector to ecef
* args   : double *pos      I   geodetic position {lat,lon} (rad)
*          double *e        I   vector in local tangental coordinate {e,n,u}
*          double *r        O   vector in ecef coordinate {x,y,z}
* return : none
*-----------------------------------------------------------------------------*/
extern void enu2ecef(const double *pos, const double *e, double *r)
{
    double E[9];
    
    xyz2enu(pos,E);
    arc_matmul("TN", 3, 1, 3, 1.0, E, e, 0.0, r);
}
/* transform covariance to local tangental coordinate --------------------------
* transform ecef covariance to local tangental coordinate
* args   : double *pos      I   geodetic position {lat,lon} (rad)
*          double *P        I   covariance in ecef coordinate
*          double *Q        O   covariance in local tangental coordinate
* return : none
*-----------------------------------------------------------------------------*/
extern void covenu(const double *pos, const double *P, double *Q)
{
    double E[9],EP[9];
    
    xyz2enu(pos,E);
    arc_matmul("NN", 3, 3, 3, 1.0, E, P, 0.0, EP);
    arc_matmul("NT", 3, 3, 3, 1.0, EP, E, 0.0, Q);
}
/* transform local enu coordinate covariance to xyz-ecef -----------------------
* transform local enu covariance to xyz-ecef coordinate
* args   : double *pos      I   geodetic position {lat,lon} (rad)
*          double *Q        I   covariance in local enu coordinate
*          double *P        O   covariance in xyz-ecef coordinate
* return : none
*-----------------------------------------------------------------------------*/
extern void covecef(const double *pos, const double *Q, double *P)
{
    double E[9],EQ[9];
    
    xyz2enu(pos,E);
    arc_matmul("TN", 3, 3, 3, 1.0, E, Q, 0.0, EQ);
    arc_matmul("NN", 3, 3, 3, 1.0, EQ, E, 0.0, P);
}
/* coordinate rotation matrix ------------------------------------------------*/
#define Rx(t,X) do { \
    (X)[0]=1.0; (X)[1]=(X)[2]=(X)[3]=(X)[6]=0.0; \
    (X)[4]=(X)[8]=cos(t); (X)[7]=sin(t); (X)[5]=-(X)[7]; \
} while (0)

#define Ry(t,X) do { \
    (X)[4]=1.0; (X)[1]=(X)[3]=(X)[5]=(X)[7]=0.0; \
    (X)[0]=(X)[8]=cos(t); (X)[2]=sin(t); (X)[6]=-(X)[2]; \
} while (0)

#define Rz(t,X) do { \
    (X)[8]=1.0; (X)[2]=(X)[5]=(X)[6]=(X)[7]=0.0; \
    (X)[0]=(X)[4]=cos(t); (X)[3]=sin(t); (X)[1]=-(X)[3]; \
} while (0)

/* astronomical arguments: f={l,l',F,D,OMG} (rad) ----------------------------*/
static void ast_args(double t, double *f)
{
    static const double fc[][5]={ /* coefficients for iau 1980 nutation */
        { 134.96340251, 1717915923.2178,  31.8792,  0.051635, -0.00024470},
        { 357.52910918,  129596581.0481,  -0.5532,  0.000136, -0.00001149},
        {  93.27209062, 1739527262.8478, -12.7512, -0.001037,  0.00000417},
        { 297.85019547, 1602961601.2090,  -6.3706,  0.006593, -0.00003169},
        { 125.04455501,   -6962890.2665,   7.4722,  0.007702, -0.00005939}
    };
    double tt[4];
    int i,j;
    
    for (tt[0]=t,i=1;i<4;i++) tt[i]=tt[i-1]*t;
    for (i=0;i<5;i++) {
        f[i]=fc[i][0]*3600.0;
        for (j=0;j<4;j++) f[i]+=fc[i][j+1]*tt[j];
        f[i]=fmod(f[i]*AS2R,2.0*PI);
    }
}
/* iau 1980 nutation ---------------------------------------------------------*/
static void nut_iau1980(double t, const double *f, double *dpsi, double *deps)
{
    static const double nut[106][10]={
        {   0,   0,   0,   0,   1, -6798.4, -171996, -174.2, 92025,   8.9},
        {   0,   0,   2,  -2,   2,   182.6,  -13187,   -1.6,  5736,  -3.1},
        {   0,   0,   2,   0,   2,    13.7,   -2274,   -0.2,   977,  -0.5},
        {   0,   0,   0,   0,   2, -3399.2,    2062,    0.2,  -895,   0.5},
        {   0,  -1,   0,   0,   0,  -365.3,   -1426,    3.4,    54,  -0.1},
        {   1,   0,   0,   0,   0,    27.6,     712,    0.1,    -7,   0.0},
        {   0,   1,   2,  -2,   2,   121.7,    -517,    1.2,   224,  -0.6},
        {   0,   0,   2,   0,   1,    13.6,    -386,   -0.4,   200,   0.0},
        {   1,   0,   2,   0,   2,     9.1,    -301,    0.0,   129,  -0.1},
        {   0,  -1,   2,  -2,   2,   365.2,     217,   -0.5,   -95,   0.3},
        {  -1,   0,   0,   2,   0,    31.8,     158,    0.0,    -1,   0.0},
        {   0,   0,   2,  -2,   1,   177.8,     129,    0.1,   -70,   0.0},
        {  -1,   0,   2,   0,   2,    27.1,     123,    0.0,   -53,   0.0},
        {   1,   0,   0,   0,   1,    27.7,      63,    0.1,   -33,   0.0},
        {   0,   0,   0,   2,   0,    14.8,      63,    0.0,    -2,   0.0},
        {  -1,   0,   2,   2,   2,     9.6,     -59,    0.0,    26,   0.0},
        {  -1,   0,   0,   0,   1,   -27.4,     -58,   -0.1,    32,   0.0},
        {   1,   0,   2,   0,   1,     9.1,     -51,    0.0,    27,   0.0},
        {  -2,   0,   0,   2,   0,  -205.9,     -48,    0.0,     1,   0.0},
        {  -2,   0,   2,   0,   1,  1305.5,      46,    0.0,   -24,   0.0},
        {   0,   0,   2,   2,   2,     7.1,     -38,    0.0,    16,   0.0},
        {   2,   0,   2,   0,   2,     6.9,     -31,    0.0,    13,   0.0},
        {   2,   0,   0,   0,   0,    13.8,      29,    0.0,    -1,   0.0},
        {   1,   0,   2,  -2,   2,    23.9,      29,    0.0,   -12,   0.0},
        {   0,   0,   2,   0,   0,    13.6,      26,    0.0,    -1,   0.0},
        {   0,   0,   2,  -2,   0,   173.3,     -22,    0.0,     0,   0.0},
        {  -1,   0,   2,   0,   1,    27.0,      21,    0.0,   -10,   0.0},
        {   0,   2,   0,   0,   0,   182.6,      17,   -0.1,     0,   0.0},
        {   0,   2,   2,  -2,   2,    91.3,     -16,    0.1,     7,   0.0},
        {  -1,   0,   0,   2,   1,    32.0,      16,    0.0,    -8,   0.0},
        {   0,   1,   0,   0,   1,   386.0,     -15,    0.0,     9,   0.0},
        {   1,   0,   0,  -2,   1,   -31.7,     -13,    0.0,     7,   0.0},
        {   0,  -1,   0,   0,   1,  -346.6,     -12,    0.0,     6,   0.0},
        {   2,   0,  -2,   0,   0, -1095.2,      11,    0.0,     0,   0.0},
        {  -1,   0,   2,   2,   1,     9.5,     -10,    0.0,     5,   0.0},
        {   1,   0,   2,   2,   2,     5.6,      -8,    0.0,     3,   0.0},
        {   0,  -1,   2,   0,   2,    14.2,      -7,    0.0,     3,   0.0},
        {   0,   0,   2,   2,   1,     7.1,      -7,    0.0,     3,   0.0},
        {   1,   1,   0,  -2,   0,   -34.8,      -7,    0.0,     0,   0.0},
        {   0,   1,   2,   0,   2,    13.2,       7,    0.0,    -3,   0.0},
        {  -2,   0,   0,   2,   1,  -199.8,      -6,    0.0,     3,   0.0},
        {   0,   0,   0,   2,   1,    14.8,      -6,    0.0,     3,   0.0},
        {   2,   0,   2,  -2,   2,    12.8,       6,    0.0,    -3,   0.0},
        {   1,   0,   0,   2,   0,     9.6,       6,    0.0,     0,   0.0},
        {   1,   0,   2,  -2,   1,    23.9,       6,    0.0,    -3,   0.0},
        {   0,   0,   0,  -2,   1,   -14.7,      -5,    0.0,     3,   0.0},
        {   0,  -1,   2,  -2,   1,   346.6,      -5,    0.0,     3,   0.0},
        {   2,   0,   2,   0,   1,     6.9,      -5,    0.0,     3,   0.0},
        {   1,  -1,   0,   0,   0,    29.8,       5,    0.0,     0,   0.0},
        {   1,   0,   0,  -1,   0,   411.8,      -4,    0.0,     0,   0.0},
        {   0,   0,   0,   1,   0,    29.5,      -4,    0.0,     0,   0.0},
        {   0,   1,   0,  -2,   0,   -15.4,      -4,    0.0,     0,   0.0},
        {   1,   0,  -2,   0,   0,   -26.9,       4,    0.0,     0,   0.0},
        {   2,   0,   0,  -2,   1,   212.3,       4,    0.0,    -2,   0.0},
        {   0,   1,   2,  -2,   1,   119.6,       4,    0.0,    -2,   0.0},
        {   1,   1,   0,   0,   0,    25.6,      -3,    0.0,     0,   0.0},
        {   1,  -1,   0,  -1,   0, -3232.9,      -3,    0.0,     0,   0.0},
        {  -1,  -1,   2,   2,   2,     9.8,      -3,    0.0,     1,   0.0},
        {   0,  -1,   2,   2,   2,     7.2,      -3,    0.0,     1,   0.0},
        {   1,  -1,   2,   0,   2,     9.4,      -3,    0.0,     1,   0.0},
        {   3,   0,   2,   0,   2,     5.5,      -3,    0.0,     1,   0.0},
        {  -2,   0,   2,   0,   2,  1615.7,      -3,    0.0,     1,   0.0},
        {   1,   0,   2,   0,   0,     9.1,       3,    0.0,     0,   0.0},
        {  -1,   0,   2,   4,   2,     5.8,      -2,    0.0,     1,   0.0},
        {   1,   0,   0,   0,   2,    27.8,      -2,    0.0,     1,   0.0},
        {  -1,   0,   2,  -2,   1,   -32.6,      -2,    0.0,     1,   0.0},
        {   0,  -2,   2,  -2,   1,  6786.3,      -2,    0.0,     1,   0.0},
        {  -2,   0,   0,   0,   1,   -13.7,      -2,    0.0,     1,   0.0},
        {   2,   0,   0,   0,   1,    13.8,       2,    0.0,    -1,   0.0},
        {   3,   0,   0,   0,   0,     9.2,       2,    0.0,     0,   0.0},
        {   1,   1,   2,   0,   2,     8.9,       2,    0.0,    -1,   0.0},
        {   0,   0,   2,   1,   2,     9.3,       2,    0.0,    -1,   0.0},
        {   1,   0,   0,   2,   1,     9.6,      -1,    0.0,     0,   0.0},
        {   1,   0,   2,   2,   1,     5.6,      -1,    0.0,     1,   0.0},
        {   1,   1,   0,  -2,   1,   -34.7,      -1,    0.0,     0,   0.0},
        {   0,   1,   0,   2,   0,    14.2,      -1,    0.0,     0,   0.0},
        {   0,   1,   2,  -2,   0,   117.5,      -1,    0.0,     0,   0.0},
        {   0,   1,  -2,   2,   0,  -329.8,      -1,    0.0,     0,   0.0},
        {   1,   0,  -2,   2,   0,    23.8,      -1,    0.0,     0,   0.0},
        {   1,   0,  -2,  -2,   0,    -9.5,      -1,    0.0,     0,   0.0},
        {   1,   0,   2,  -2,   0,    32.8,      -1,    0.0,     0,   0.0},
        {   1,   0,   0,  -4,   0,   -10.1,      -1,    0.0,     0,   0.0},
        {   2,   0,   0,  -4,   0,   -15.9,      -1,    0.0,     0,   0.0},
        {   0,   0,   2,   4,   2,     4.8,      -1,    0.0,     0,   0.0},
        {   0,   0,   2,  -1,   2,    25.4,      -1,    0.0,     0,   0.0},
        {  -2,   0,   2,   4,   2,     7.3,      -1,    0.0,     1,   0.0},
        {   2,   0,   2,   2,   2,     4.7,      -1,    0.0,     0,   0.0},
        {   0,  -1,   2,   0,   1,    14.2,      -1,    0.0,     0,   0.0},
        {   0,   0,  -2,   0,   1,   -13.6,      -1,    0.0,     0,   0.0},
        {   0,   0,   4,  -2,   2,    12.7,       1,    0.0,     0,   0.0},
        {   0,   1,   0,   0,   2,   409.2,       1,    0.0,     0,   0.0},
        {   1,   1,   2,  -2,   2,    22.5,       1,    0.0,    -1,   0.0},
        {   3,   0,   2,  -2,   2,     8.7,       1,    0.0,     0,   0.0},
        {  -2,   0,   2,   2,   2,    14.6,       1,    0.0,    -1,   0.0},
        {  -1,   0,   0,   0,   2,   -27.3,       1,    0.0,    -1,   0.0},
        {   0,   0,  -2,   2,   1,  -169.0,       1,    0.0,     0,   0.0},
        {   0,   1,   2,   0,   1,    13.1,       1,    0.0,     0,   0.0},
        {  -1,   0,   4,   0,   2,     9.1,       1,    0.0,     0,   0.0},
        {   2,   1,   0,  -2,   0,   131.7,       1,    0.0,     0,   0.0},
        {   2,   0,   0,   2,   0,     7.1,       1,    0.0,     0,   0.0},
        {   2,   0,   2,  -2,   1,    12.8,       1,    0.0,    -1,   0.0},
        {   2,   0,  -2,   0,   1,  -943.2,       1,    0.0,     0,   0.0},
        {   1,  -1,   0,  -2,   0,   -29.3,       1,    0.0,     0,   0.0},
        {  -1,   0,   0,   1,   1,  -388.3,       1,    0.0,     0,   0.0},
        {  -1,  -1,   0,   2,   1,    35.0,       1,    0.0,     0,   0.0},
        {   0,   1,   0,   1,   0,    27.3,       1,    0.0,     0,   0.0}
    };
    double ang;
    int i,j;
    
    *dpsi=*deps=0.0;
    
    for (i=0;i<106;i++) {
        ang=0.0;
        for (j=0;j<5;j++) ang+=nut[i][j]*f[j];
        *dpsi+=(nut[i][6]+nut[i][7]*t)*sin(ang);
        *deps+=(nut[i][8]+nut[i][9]*t)*cos(ang);
    }
    *dpsi*=1E-4*AS2R; /* 0.1 mas -> rad */
    *deps*=1E-4*AS2R;
}
/* eci to ecef transformation matrix -------------------------------------------
* compute eci to ecef transformation matrix
* args   : gtime_t tutc     I   time in utc
*          double *erpv     I   erp values {xp,yp,ut1_utc,lod} (rad,rad,s,s/d)
*          double *U        O   eci to ecef transformation matrix (3 x 3)
*          double *gmst     IO  greenwich mean sidereal time (rad)
*                               (NULL: no output)
* return : none
* note   : see ref [3] chap 5
*          not thread-safe
*-----------------------------------------------------------------------------*/
extern void eci2ecef(gtime_t tutc, const double *erpv, double *U, double *gmst)
{
    const double ep2000[]={2000,1,1,12,0,0};
    static gtime_t tutc_;
    static double U_[9],gmst_;
    gtime_t tgps;
    double eps,ze,th,z,t,t2,t3,dpsi,deps,gast,f[5];
    double R1[9],R2[9],R3[9],R[9],W[9],N[9],P[9],NP[9];
    int i;

    arc_log(4, "eci2ecef: tutc=%s\n", time_str(tutc, 3));
    
    if (fabs(timediff(tutc,tutc_))<0.01) { /* read cache */
        for (i=0;i<9;i++) U[i]=U_[i];
        if (gmst) *gmst=gmst_; 
        return;
    }
    tutc_=tutc;
    
    /* terrestrial time */
    tgps=utc2gpst(tutc_);
    t=(timediff(tgps,epoch2time(ep2000))+19.0+32.184)/86400.0/36525.0;
    t2=t*t; t3=t2*t;
    
    /* astronomical arguments */
    ast_args(t,f);
    
    /* iau 1976 precession */
    ze=(2306.2181*t+0.30188*t2+0.017998*t3)*AS2R;
    th=(2004.3109*t-0.42665*t2-0.041833*t3)*AS2R;
    z =(2306.2181*t+1.09468*t2+0.018203*t3)*AS2R;
    eps=(84381.448-46.8150*t-0.00059*t2+0.001813*t3)*AS2R;
    Rz(-z,R1); Ry(th,R2); Rz(-ze,R3);
    arc_matmul("NN", 3, 3, 3, 1.0, R1, R2, 0.0, R);
    arc_matmul("NN", 3, 3, 3, 1.0, R, R3, 0.0, P); /* P=Rz(-z)*Ry(th)*Rz(-ze) */
    
    /* iau 1980 nutation */
    nut_iau1980(t,f,&dpsi,&deps);
    Rx(-eps-deps,R1); Rz(-dpsi,R2); Rx(eps,R3);
    arc_matmul("NN", 3, 3, 3, 1.0, R1, R2, 0.0, R);
    arc_matmul("NN", 3, 3, 3, 1.0, R, R3, 0.0, N); /* N=Rx(-eps)*Rz(-dspi)*Rx(eps) */
    
    /* greenwich aparent sidereal time (rad) */
    gmst_=utc2gmst(tutc_,erpv[2]);
    gast=gmst_+dpsi*cos(eps);
    gast+=(0.00264*sin(f[4])+0.000063*sin(2.0*f[4]))*AS2R;
    
    /* eci to ecef transformation matrix */
    Ry(-erpv[0],R1); Rx(-erpv[1],R2); Rz(gast,R3);
    arc_matmul("NN", 3, 3, 3, 1.0, R1, R2, 0.0, W);
    arc_matmul("NN", 3, 3, 3, 1.0, W, R3, 0.0, R); /* W=Ry(-xp)*Rx(-yp) */
    arc_matmul("NN", 3, 3, 3, 1.0, N, P, 0.0, NP);
    arc_matmul("NN", 3, 3, 3, 1.0, R, NP, 0.0, U_); /* U=W*Rz(gast)*N*P */
    
    for (i=0;i<9;i++) U[i]=U_[i];
    if (gmst) *gmst=gmst_;

    arc_log(5, "gmst=%.12f gast=%.12f\n", gmst_, gast);
    arc_log(5, "P=\n");
    arc_tracemat(5, P, 3, 3, 15, 12);
    arc_log(5, "N=\n");
    arc_tracemat(5, N, 3, 3, 15, 12);
    arc_log(5, "W=\n");
    arc_tracemat(5, W, 3, 3, 15, 12);
    arc_log(5, "U=\n");
    arc_tracemat(5, U, 3, 3, 15, 12);
}
/* decode antenna parameter field --------------------------------------------*/
static int arc_decodef(char *p, int n, double *v)
{
    int i;
    
    for (i=0;i<n;i++) v[i]=0.0;
    for (i=0,p=strtok(p," ");p&&i<n;p=strtok(NULL," ")) {
        v[i++]=atof(p)*1E-3;
    }
    return i;
}
/* add antenna parameter -----------------------------------------------------*/
static void arc_addpcv(const pcv_t *pcv, pcvs_t *pcvs)
{
    pcv_t *pcvs_pcv;
    
    if (pcvs->nmax<=pcvs->n) {
        pcvs->nmax+=256;
        if (!(pcvs_pcv=(pcv_t *)realloc(pcvs->pcv,sizeof(pcv_t)*pcvs->nmax))) {
            arc_log(1, "addpcv: memory allocation error\n");
            free(pcvs->pcv); pcvs->pcv=NULL; pcvs->n=pcvs->nmax=0;
            return;
        }
        pcvs->pcv=pcvs_pcv;
    }
    pcvs->pcv[pcvs->n++]=*pcv;
}
/* read ngs antenna parameter file -------------------------------------------*/
static int arc_readngspcv(const char *file, pcvs_t *pcvs)
{
    FILE *fp;
    static const pcv_t pcv0={0};
    pcv_t pcv;
    double neu[3];
    int n=0;
    char buff[256];
    
    if (!(fp=fopen(file,"r"))) {
        arc_log(2, "ngs pcv file open error: %s\n", file);
        return 0;
    }
    while (fgets(buff,sizeof(buff),fp)) {
        
        if (strlen(buff)>=62&&buff[61]=='|') continue;
        
        if (buff[0]!=' ') n=0; /* start line */
        if (++n==1) {
            pcv=pcv0;
            strncpy(pcv.type,buff,61); pcv.type[61]='\0';
        }
        else if (n==2) {
            if (arc_decodef(buff,3,neu)<3) continue;
            pcv.off[0][0]=neu[1];
            pcv.off[0][1]=neu[0];
            pcv.off[0][2]=neu[2];
        }
        else if (n==3) arc_decodef(buff,10,pcv.var[0]);
        else if (n==4) arc_decodef(buff,9,pcv.var[0]+10);
        else if (n==5) {
            if (arc_decodef(buff,3,neu)<3) continue;;
            pcv.off[1][0]=neu[1];
            pcv.off[1][1]=neu[0];
            pcv.off[1][2]=neu[2];
        }
        else if (n==6) arc_decodef(buff,10,pcv.var[1]);
        else if (n==7) {
            arc_decodef(buff,9,pcv.var[1]+10);
            arc_addpcv(&pcv,pcvs);
        }
    }
    fclose(fp);
    
    return 1;
}
/* read antex file ----------------------------------------------------------*/
static int readantex(const char *file, pcvs_t *pcvs)
{
    FILE *fp;
    static const pcv_t pcv0={0};
    pcv_t pcv;
    double neu[3];
    int i,f,freq=0,state=0,freqs[]={1,2,5,6,7,8,0};
    char buff[256];

    arc_log(ARC_INFO, "readantex: file=%s\n", file);
    
    if (!(fp=fopen(file,"r"))) {
        arc_log(2, "antex pcv file open error: %s\n", file);
        return 0;
    }
    while (fgets(buff,sizeof(buff),fp)) {
        
        if (strlen(buff)<60||strstr(buff+60,"COMMENT")) continue;
        
        if (strstr(buff+60,"START OF ANTENNA")) {
            pcv=pcv0;
            state=1;
        }
        if (strstr(buff+60,"END OF ANTENNA")) {
            arc_addpcv(&pcv,pcvs);
            state=0;
        }
        if (!state) continue;
        
        if (strstr(buff+60,"TYPE / SERIAL NO")) {
            strncpy(pcv.type,buff   ,20); pcv.type[20]='\0';
            strncpy(pcv.code,buff+20,20); pcv.code[20]='\0';
            if (!strncmp(pcv.code+3,"        ",8)) {
                pcv.sat=satid2no(pcv.code);
            }
        }
        else if (strstr(buff+60,"VALID FROM")) {
            if (!str2time(buff,0,43,&pcv.ts)) continue;
        }
        else if (strstr(buff+60,"VALID UNTIL")) {
            if (!str2time(buff,0,43,&pcv.te)) continue;
        }
        else if (strstr(buff+60,"START OF FREQUENCY")) {
            if (sscanf(buff+4,"%d",&f)<1) continue;
            for (i=0;i<NFREQ;i++) if (freqs[i]==f) break;
            if (i<NFREQ) freq=i+1;
        }
        else if (strstr(buff+60,"END OF FREQUENCY")) {
            freq=0;
        }
        else if (strstr(buff+60,"NORTH / EAST / UP")) {
            if (freq<1||NFREQ<freq) continue;
            if (arc_decodef(buff,3,neu)<3) continue;
            pcv.off[freq-1][0]=neu[pcv.sat?0:1]; /* x or e */
            pcv.off[freq-1][1]=neu[pcv.sat?1:0]; /* y or n */
            pcv.off[freq-1][2]=neu[2];           /* z or u */
        }
        else if (strstr(buff,"NOAZI")) {
            if (freq<1||NFREQ<freq) continue;
            if ((i=arc_decodef(buff+8,19,pcv.var[freq-1]))<=0) continue;
            for (;i<19;i++) pcv.var[freq-1][i]=pcv.var[freq-1][i-1];
        }
    }
    fclose(fp);
    
    return 1;
}
/* read antenna parameters ------------------------------------------------------
* read antenna parameters
* args   : char   *file       I   antenna parameter file (antex)
*          pcvs_t *pcvs       IO  antenna parameters
* return : status (1:ok,0:file open error)
* notes  : file with the externsion .atx or .ATX is recognized as antex
*          file except for antex is recognized ngs antenna parameters
*          see reference [3]
*          only support non-azimuth-depedent parameters
*-----------------------------------------------------------------------------*/
extern int arc_readpcv(const char *file, pcvs_t *pcvs)
{
    pcv_t *pcv;
    char *ext,file_[1024];
    int i,stat;

    arc_log(ARC_INFO, "readpcv: file=%s\n", file);
    
	strcpy(file_,file);
    if (!(ext=strrchr(file_,'.'))) ext="";
    
    if (!strcmp(ext,".atx")||!strcmp(ext,".ATX")) {
        stat=readantex(file_,pcvs);
    }
    else {
        stat=arc_readngspcv(file_,pcvs);
    }
    for (i=0;i<pcvs->n;i++) {
        pcv=pcvs->pcv+i;
        arc_log(ARC_INFO, "sat=%2d type=%20s code=%s off=%8.4f %8.4f %8.4f  %8.4f %8.4f %8.4f\n",
                pcv->sat, pcv->type, pcv->code, pcv->off[0][0], pcv->off[0][1],
                pcv->off[0][2], pcv->off[1][0], pcv->off[1][1], pcv->off[1][2]);
    }
    return stat;
}
/* search antenna parameter ----------------------------------------------------
* read satellite antenna phase center position
* args   : int    sat         I   satellite number (0: receiver antenna)
*          char   *type       I   antenna type for receiver antenna
*          gtime_t time       I   time to search parameters
*          pcvs_t *pcvs       IO  antenna parameters
* return : antenna parameter (NULL: no antenna)
*-----------------------------------------------------------------------------*/
extern pcv_t *arc_searchpcv(int sat, const char *type, gtime_t time,
                            const pcvs_t *pcvs)
{
    pcv_t *pcv;
    char buff[MAXANT],*types[2],*p;
    int i,j,n=0;

    arc_log(ARC_INFO, "searchpcv: sat=%2d type=%s\n", sat, type);
    
    if (sat) { /* search satellite antenna */
        for (i=0;i<pcvs->n;i++) {
            pcv=pcvs->pcv+i;
            if (pcv->sat!=sat) continue;
            if (pcv->ts.time!=0&&timediff(pcv->ts,time)>0.0) continue;
            if (pcv->te.time!=0&&timediff(pcv->te,time)<0.0) continue;
            return pcv;
        }
    }
    else {
        strcpy(buff,type);
        for (p=strtok(buff," ");p&&n<2;p=strtok(NULL," ")) types[n++]=p;
        if (n<=0) return NULL;
        
        /* search receiver antenna with radome at first */
        for (i=0;i<pcvs->n;i++) {
            pcv=pcvs->pcv+i;
            for (j=0;j<n;j++) if (!strstr(pcv->type,types[j])) break;
            if (j>=n) return pcv;
        }
        /* search receiver antenna without radome */
        for (i=0;i<pcvs->n;i++) {
            pcv=pcvs->pcv+i;
            if (strstr(pcv->type,types[0])!=pcv->type) continue;

            arc_log(2, "pcv without radome is used type=%s\n", type);
            return pcv;
        }
    }
    return NULL;
}
/* read station positions ------------------------------------------------------
* read positions from station position file
* args   : char  *file      I   station position file containing
*                               lat(deg) lon(deg) height(m) name in a line
*          char  *rcvs      I   station name
*          double *pos      O   station position {lat,lon,h} (rad/m)
*                               (all 0 if search error)
* return : none
*-----------------------------------------------------------------------------*/
extern void readpos(const char *file, const char *rcv, double *pos)
{
    static double poss[2048][3];
    static char stas[2048][16];
    FILE *fp;
    int i,j,len,np=0;
    char buff[256],str[256];

    arc_log(ARC_INFO, "readpos: file=%s\n", file);
    
    if (!(fp=fopen(file,"r"))) {
        fprintf(stderr,"reference position file open error : %s\n",file);
        return;
    }
    while (np<2048&&fgets(buff,sizeof(buff),fp)) {
        if (buff[0]=='%'||buff[0]=='#') continue;
        if (sscanf(buff,"%lf %lf %lf %s",&poss[np][0],&poss[np][1],&poss[np][2],
                   str)<4) continue;
        strncpy(stas[np],str,15); stas[np++][15]='\0';
    }
    fclose(fp);
    len=(int)strlen(rcv);
    for (i=0;i<np;i++) {
        if (strncmp(stas[i],rcv,len)) continue;
        for (j=0;j<3;j++) pos[j]=poss[i][j];
        pos[0]*=D2R; pos[1]*=D2R;
        return;
    }
    pos[0]=pos[1]=pos[2]=0.0;
}
/* read blq record -----------------------------------------------------------*/
static int arc_readblqrecord(FILE *fp, double *odisp)
{
    double v[11];
    char buff[256];
    int i,n=0;
    
    while (fgets(buff,sizeof(buff),fp)) {
        if (!strncmp(buff,"$$",2)) continue;
        if (sscanf(buff,"%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                   v,v+1,v+2,v+3,v+4,v+5,v+6,v+7,v+8,v+9,v+10)<11) continue;
        for (i=0;i<11;i++) odisp[n+i*6]=v[i];
        if (++n==6) return 1;
    }
    return 0;
}
/* read blq ocean tide loading parameters --------------------------------------
* read blq ocean tide loading parameters
* args   : char   *file       I   BLQ ocean tide loading parameter file
*          char   *sta        I   station name
*          double *odisp      O   ocean tide loading parameters
* return : status (1:ok,0:file open error)
*-----------------------------------------------------------------------------*/
extern int readblq(const char *file, const char *sta, double *odisp)
{
    FILE *fp;
    char buff[256],staname[32]="",name[32],*p;
    
    /* station name to upper case */
    sscanf(sta,"%16s",staname);
    for (p=staname;(*p=(char)toupper((int)(*p)));p++) ;
    
    if (!(fp=fopen(file,"r"))) {
        arc_log(2, "blq file open error: file=%s\n", file);
        return 0;
    }
    while (fgets(buff,sizeof(buff),fp)) {
        if (!strncmp(buff,"$$",2)||strlen(buff)<2) continue;
        
        if (sscanf(buff+2,"%16s",name)<1) continue;
        for (p=name;(*p=(char)toupper((int)(*p)));p++) ;
        if (strcmp(name,staname)) continue;
        
        /* read blq record */
        if (arc_readblqrecord(fp,odisp)) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    arc_log(2, "no otl parameters: sta=%s file=%s\n", sta, file);
    return 0;
}
/* read earth rotation parameters ----------------------------------------------
* read earth rotation parameters
* args   : char   *file       I   IGS ERP file (IGS ERP ver.2)
*          erp_t  *erp        O   earth rotation parameters
* return : status (1:ok,0:file open error)
*-----------------------------------------------------------------------------*/
extern int readerp(const char *file, erp_t *erp)
{
    FILE *fp;
    erpd_t *erp_data;
    double v[14]={0};
    char buff[256];

    arc_log(ARC_INFO, "readerp: file=%s\n", file);
    
    if (!(fp=fopen(file,"r"))) {
        arc_log(2, "erp file open error: file=%s\n", file);
        return 0;
    }
    while (fgets(buff,sizeof(buff),fp)) {
        if (sscanf(buff,"%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                   v,v+1,v+2,v+3,v+4,v+5,v+6,v+7,v+8,v+9,v+10,v+11,v+12,v+13)<5) {
            continue;
        }
        if (erp->n>=erp->nmax) {
            erp->nmax=erp->nmax<=0?128:erp->nmax*2;
            erp_data=(erpd_t *)realloc(erp->data,sizeof(erpd_t)*erp->nmax);
            if (!erp_data) {
                free(erp->data); erp->data=NULL; erp->n=erp->nmax=0;
                fclose(fp);
                return 0;
            }
            erp->data=erp_data;
        }
        erp->data[erp->n].mjd=v[0];
        erp->data[erp->n].xp=v[1]*1E-6*AS2R;
        erp->data[erp->n].yp=v[2]*1E-6*AS2R;
        erp->data[erp->n].ut1_utc=v[3]*1E-7;
        erp->data[erp->n].lod=v[4]*1E-7;
        erp->data[erp->n].xpr=v[12]*1E-6*AS2R;
        erp->data[erp->n++].ypr=v[13]*1E-6*AS2R;
    }
    fclose(fp);
    return 1;
}
/* get earth rotation parameter values -----------------------------------------
* get earth rotation parameter values
* args   : erp_t  *erp        I   earth rotation parameters
*          gtime_t time       I   time (gpst)
*          double *erpv       O   erp values {xp,yp,ut1_utc,lod} (rad,rad,s,s/d)
* return : status (1:ok,0:error)
*-----------------------------------------------------------------------------*/
extern int geterp(const erp_t *erp, gtime_t time, double *erpv)
{
    const double ep[]={2000,1,1,12,0,0};
    double mjd,day,a;
    int i,j,k;

    arc_log(4, "geterp:\n");
    
    if (erp->n<=0) return 0;
    
    mjd=51544.5+(timediff(gpst2utc(time),epoch2time(ep)))/86400.0;
    
    if (mjd<=erp->data[0].mjd) {
        day=mjd-erp->data[0].mjd;
        erpv[0]=erp->data[0].xp     +erp->data[0].xpr*day;
        erpv[1]=erp->data[0].yp     +erp->data[0].ypr*day;
        erpv[2]=erp->data[0].ut1_utc-erp->data[0].lod*day;
        erpv[3]=erp->data[0].lod;
        return 1;
    }
    if (mjd>=erp->data[erp->n-1].mjd) {
        day=mjd-erp->data[erp->n-1].mjd;
        erpv[0]=erp->data[erp->n-1].xp     +erp->data[erp->n-1].xpr*day;
        erpv[1]=erp->data[erp->n-1].yp     +erp->data[erp->n-1].ypr*day;
        erpv[2]=erp->data[erp->n-1].ut1_utc-erp->data[erp->n-1].lod*day;
        erpv[3]=erp->data[erp->n-1].lod;
        return 1;
    }
    for (j=0,k=erp->n-1;j<k-1;) {
        i=(j+k)/2;
        if (mjd<erp->data[i].mjd) k=i; else j=i;
    }
    if (erp->data[j].mjd==erp->data[j+1].mjd) {
        a=0.5;
    }
    else {
        a=(mjd-erp->data[j].mjd)/(erp->data[j+1].mjd-erp->data[j].mjd);
    }
    erpv[0]=(1.0-a)*erp->data[j].xp     +a*erp->data[j+1].xp;
    erpv[1]=(1.0-a)*erp->data[j].yp     +a*erp->data[j+1].yp;
    erpv[2]=(1.0-a)*erp->data[j].ut1_utc+a*erp->data[j+1].ut1_utc;
    erpv[3]=(1.0-a)*erp->data[j].lod    +a*erp->data[j+1].lod;
    return 1;
}
/* compare ephemeris ---------------------------------------------------------*/
static int arc_cmpeph(const void *p1, const void *p2)
{
    eph_t *q1=(eph_t *)p1,*q2=(eph_t *)p2;
    return q1->ttr.time!=q2->ttr.time?(int)(q1->ttr.time-q2->ttr.time):
           (q1->toe.time!=q2->toe.time?(int)(q1->toe.time-q2->toe.time):
            q1->sat-q2->sat);
}
/* sort and unique ephemeris -------------------------------------------------*/
static void arc_uniqeph(nav_t *nav)
{
    eph_t *nav_eph;
    int i,j;

    arc_log(ARC_INFO, "uniqeph: n=%d\n", nav->n);
    
    if (nav->n<=0) return;
    
    qsort(nav->eph,nav->n,sizeof(eph_t),arc_cmpeph);
    
    for (i=1,j=0;i<nav->n;i++) {
        if (nav->eph[i].sat!=nav->eph[j].sat||
            nav->eph[i].iode!=nav->eph[j].iode) {
            nav->eph[++j]=nav->eph[i];
        }
    }
    nav->n=j+1;
    
    if (!(nav_eph=(eph_t *)realloc(nav->eph,sizeof(eph_t)*nav->n))) {
        arc_log(1, "uniqeph malloc error n=%d\n", nav->n);
        free(nav->eph); nav->eph=NULL; nav->n=nav->nmax=0;
        return;
    }
    nav->eph=nav_eph;
    nav->nmax=nav->n;

    arc_log(ARC_INFO, "uniqeph: n=%d\n", nav->n);
}
/* compare glonass ephemeris -------------------------------------------------*/
static int cmpgeph(const void *p1, const void *p2)
{
    geph_t *q1=(geph_t *)p1,*q2=(geph_t *)p2;
    return q1->tof.time!=q2->tof.time?(int)(q1->tof.time-q2->tof.time):
           (q1->toe.time!=q2->toe.time?(int)(q1->toe.time-q2->toe.time):
            q1->sat-q2->sat);
}
/* sort and unique glonass ephemeris -----------------------------------------*/
static void arc_uniqgeph(nav_t *nav)
{
    geph_t *nav_geph;
    int i,j;

    arc_log(ARC_INFO, "uniqgeph: ng=%d\n", nav->ng);
    
    if (nav->ng<=0) return;
    
    qsort(nav->geph,nav->ng,sizeof(geph_t),cmpgeph);
    
    for (i=j=0;i<nav->ng;i++) {
        if (nav->geph[i].sat!=nav->geph[j].sat||
            nav->geph[i].toe.time!=nav->geph[j].toe.time||
            nav->geph[i].svh!=nav->geph[j].svh) {
            nav->geph[++j]=nav->geph[i];
        }
    }
    nav->ng=j+1;
    
    if (!(nav_geph=(geph_t *)realloc(nav->geph,sizeof(geph_t)*nav->ng))) {
        arc_log(1, "uniqgeph malloc error ng=%d\n", nav->ng);
        free(nav->geph); nav->geph=NULL; nav->ng=nav->ngmax=0;
        return;
    }
    nav->geph=nav_geph;
    nav->ngmax=nav->ng;

    arc_log(ARC_INFO, "uniqgeph: ng=%d\n", nav->ng);
}
/* compare sbas ephemeris ----------------------------------------------------*/
static int arc_cmpseph(const void *p1, const void *p2)
{
    seph_t *q1=(seph_t *)p1,*q2=(seph_t *)p2;
    return q1->tof.time!=q2->tof.time?(int)(q1->tof.time-q2->tof.time):
           (q1->t0.time!=q2->t0.time?(int)(q1->t0.time-q2->t0.time):
            q1->sat-q2->sat);
}
/* sort and unique sbas ephemeris --------------------------------------------*/
static void arc_uniqseph(nav_t *nav)
{
    seph_t *nav_seph;
    int i,j;

    arc_log(ARC_INFO, "uniqseph: ns=%d\n", nav->ns);
    
    if (nav->ns<=0) return;
    
    qsort(nav->seph,nav->ns,sizeof(seph_t),arc_cmpseph);
    
    for (i=j=0;i<nav->ns;i++) {
        if (nav->seph[i].sat!=nav->seph[j].sat||
            nav->seph[i].t0.time!=nav->seph[j].t0.time) {
            nav->seph[++j]=nav->seph[i];
        }
    }
    nav->ns=j+1;
    
    if (!(nav_seph=(seph_t *)realloc(nav->seph,sizeof(seph_t)*nav->ns))) {
        arc_log(ARC_WARNING, "uniqseph malloc error ns=%d\n", nav->ns);
        free(nav->seph); nav->seph=NULL; nav->ns=nav->nsmax=0;
        return;
    }
    nav->seph=nav_seph;
    nav->nsmax=nav->ns;

    arc_log(ARC_INFO, "uniqseph: ns=%d\n", nav->ns);
}
/* unique ephemerides ----------------------------------------------------------
* unique ephemerides in navigation data and update carrier wave length
* args   : nav_t *nav    IO     navigation data
* return : number of epochs
*-----------------------------------------------------------------------------*/
extern void uniqnav(nav_t *nav)
{
    int i,j;

    arc_log(ARC_INFO, "uniqnav: neph=%d ngeph=%d nseph=%d\n", nav->n, nav->ng, nav->ns);
    
    /* unique ephemeris */
    arc_uniqeph (nav);
    arc_uniqgeph(nav);
    arc_uniqseph(nav);
    
    /* update carrier wave length */
    for (i=0;i<MAXSAT;i++) for (j=0;j<NFREQ;j++) {
        nav->lam[i][j]= arc_satwavelen(i + 1, j, nav);
    }
}
/* compare observation data -------------------------------------------------*/
static int arc_cmpobs(const void *p1, const void *p2)
{
    obsd_t *q1=(obsd_t *)p1,*q2=(obsd_t *)p2;
    double tt=timediff(q1->time,q2->time);
    if (fabs(tt)>DTTOL) return tt<0?-1:1;
    if (q1->rcv!=q2->rcv) return (int)q1->rcv-(int)q2->rcv;
    return (int)q1->sat-(int)q2->sat;
}
/* sort and unique observation data --------------------------------------------
* sort and unique observation data by time, rcv, sat
* args   : obs_t *obs    IO     observation data
* return : number of epochs
*-----------------------------------------------------------------------------*/
extern int sortobs(obs_t *obs)
{
    int i,j,n;

    arc_log(ARC_INFO, "sortobs: nobs=%d\n", obs->n);
    
    if (obs->n<=0) return 0;
    
    qsort(obs->data,obs->n,sizeof(obsd_t),arc_cmpobs);
    
    /* delete duplicated data */
    for (i=j=0;i<obs->n;i++) {
        if (obs->data[i].sat!=obs->data[j].sat||
            obs->data[i].rcv!=obs->data[j].rcv||
            timediff(obs->data[i].time,obs->data[j].time)!=0.0) {
            obs->data[++j]=obs->data[i];
        }
    }
    obs->n=j+1;
    
    for (i=n=0;i<obs->n;i=j,n++) {
        for (j=i+1;j<obs->n;j++) {
            if (timediff(obs->data[j].time,obs->data[i].time)>DTTOL) break;
        }
    }
    return n;
}
/* screen by time --------------------------------------------------------------
* screening by time start, time end, and time interval
* args   : gtime_t time  I      time
*          gtime_t ts    I      time start (ts.time==0:no screening by ts)
*          gtime_t te    I      time end   (te.time==0:no screening by te)
*          double  tint  I      time interval (s) (0.0:no screen by tint)
* return : 1:on condition, 0:not on condition
*-----------------------------------------------------------------------------*/
extern int screent(gtime_t time, gtime_t ts, gtime_t te, double tint)
{
    return (tint<=0.0||fmod(time2gpst(time,NULL)+DTTOL,tint)<=DTTOL*2.0)&&
           (ts.time==0||timediff(time,ts)>=-DTTOL)&&
           (te.time==0||timediff(time,te)<  DTTOL);
}
/* free observation data -------------------------------------------------------
* free memory for observation data
* args   : obs_t *obs    IO     observation data
* return : none
*-----------------------------------------------------------------------------*/
extern void freeobs(obs_t *obs)
{
    free(obs->data); obs->data=NULL; obs->n=obs->nmax=0;
}
/* free navigation data ---------------------------------------------------------
* free memory for navigation data
* args   : nav_t *nav    IO     navigation data
*          int   opt     I      option (or of followings)
*                               (0x01: gps/qzs ephmeris, 0x02: glonass ephemeris,
*                                0x04: sbas ephemeris,   0x08: precise ephemeris,
*                                0x10: precise clock     0x20: almanac,
*                                0x40: tec data)
* return : none
*-----------------------------------------------------------------------------*/
extern void freenav(nav_t *nav, int opt)
{
    if (opt&0x01) {free(nav->eph ); nav->eph =NULL; nav->n =nav->nmax =0;}
    if (opt&0x02) {free(nav->geph); nav->geph=NULL; nav->ng=nav->ngmax=0;}
    if (opt&0x04) {free(nav->seph); nav->seph=NULL; nav->ns=nav->nsmax=0;}
    if (opt&0x08) {free(nav->peph); nav->peph=NULL; nav->ne=nav->nemax=0;}
    if (opt&0x10) {free(nav->pclk); nav->pclk=NULL; nav->nc=nav->ncmax=0;}
    if (opt&0x20) {free(nav->alm ); nav->alm =NULL; nav->na=nav->namax=0;}
    if (opt&0x80) {free(nav->fcb ); nav->fcb =NULL; nav->nf=nav->nfmax=0;}
}
/* debug arc_log functions -----------------------------------------------------*/
#ifdef TRACE

static FILE *fp_trace=NULL;       /* file pointer of arc_log */
static char file_trace[1024];     /* arc_log file */
static int level_trace=0;         /* level of arc_log */
static unsigned int tick_trace=0; /* tick time at arc_traceopen (ms) */
static gtime_t time_trace={0};    /* time at arc_traceopen */
static lock_t lock_trace;         /* lock for arc_log */
static char s[1024];              /* log informations */
static int glog_init=1;           /* google log initial flag */
static int count=0;               /* how many count to output */
static int buffcount=1;           
static char logfile[1024];        /* google log file path */
static int glog_output_file=0;    /* google log information to file */

static void traceswap(void)
{
    gtime_t time=utc2gpst(timeget());
    char path[1024];
    
    lock(&lock_trace);
    
    if ((int)(time2gpst(time      ,NULL)/INT_SWAP_TRAC)==
        (int)(time2gpst(time_trace,NULL)/INT_SWAP_TRAC)) {
        unlock(&lock_trace);
        return;
    }
    time_trace=time;
    
    if (!reppath(file_trace,path,time,"","")) {
        unlock(&lock_trace);
        return;
    }
    if (fp_trace) fclose(fp_trace);
    
    if (!(fp_trace=fopen(path,"w"))) {
        fp_trace=stderr;
    }
    unlock(&lock_trace);
}
extern void arc_traceopen(const char *file)
{
    gtime_t time=utc2gpst(timeget());
    char path[1024];
    
    reppath(file,path,time,"","");
    if (!*path||!(fp_trace=fopen(path,"w"))) fp_trace=stderr;
    strcpy(file_trace,file);
    tick_trace=tickget();
    time_trace=time;
    initlock(&lock_trace);
}
extern void arc_traceclose(void)
{
    if (fp_trace&&fp_trace!=stderr) fclose(fp_trace);
    fp_trace=NULL;
    file_trace[0]='\0';
}
extern void arc_tracelevel(int level)
{
    level_trace=level;
}
extern void arc_tracebuf(int count)
{
    buffcount=count;
}
extern void arc_set_glog_tofile(int opt)
{
    glog_output_file=opt;
}
extern void arc_log(int level, const char *format, ...)
{
    va_list ap;
    if (level_trace==ARC_NOLOG&&(level!=ARC_FATAL
		||level!=ARC_ERROR)) return;
#if GLOG
	if (glog_init) {
		google::InitGoogleLogging("");
#ifdef IF_DEBUG_MODE
        google::SetStderrLogging(google::GLOG_INFO); 
#else
        google::SetStderrLogging(google::GLOG_FATAL);
#endif
        FLAGS_max_log_size=100;
        FLAGS_stop_logging_if_full_disk=true;
		FLAGS_colorlogtostderr=true;
        if (glog_output_file) {
            FLAGS_log_dir=".";
            google::SetLogDestination(google::GLOG_FATAL, "arc_log_fatal_");
            google::SetLogDestination(google::GLOG_ERROR, "arc_log_error_");
            google::SetLogDestination(google::GLOG_WARNING, "arc_log_warning_");
            google::SetLogDestination(google::GLOG_INFO, "arc_log_info_");
        }
        google::InstallFailureSignalHandler();
		glog_init=0;
	}
	va_start(ap,format);
	vsprintf(s,format,ap);
	va_end(ap);
	if ((count++>=buffcount)&&level==ARC_INFO&&level>=level_trace) {
		LOG(INFO)   <<s; count=0;
	}
    if (level==ARC_WARNING&&level>=level_trace) LOG(WARNING)<<s;
	if (level==ARC_ERROR&&level>=level_trace) LOG(ERROR ) <<s;
	if (level==ARC_FATAL&&level>=level_trace) LOG(FATAL ) <<s;
#endif
	/* print error message to stderr */
	if (level_trace<ARC_LOGFILE) return;

    if (!fp_trace||level>level_trace) return;
    traceswap();
    fprintf(fp_trace,"%d ",level);
    va_start(ap,format); vfprintf(fp_trace,format,ap); va_end(ap);
    fflush(fp_trace);
}
extern void arc_tracet(int level, const char *format, ...)
{
    va_list ap;
    
    if (!fp_trace||level>level_trace) return;
    traceswap();
    fprintf(fp_trace,"%d %9.3f: ",level,(tickget()-tick_trace)/1000.0);
    va_start(ap,format); vfprintf(fp_trace,format,ap); va_end(ap);
    fflush(fp_trace);
}
extern void arc_tracemat(int level, const double *A, int n, int m, int p, int q)
{
    if (level!=ARC_MATPRINTF) return;
    if (A==NULL) return;
#ifdef ARC_TRACE_MAT
    matfprint(A,n,m,p,q,stderr); fflush(stderr);
#endif
}
extern void arc_tracemati(int level,const int *A,int n,int m,int p,int q)
{
    if (level!=ARC_MATPRINTF) return;
#ifdef ARC_TRACE_MAT
    matfprinti(A,n,m,p,q,stderr); fflush(stderr);
#endif
}
extern void arc_traceobs(int level, const obsd_t *obs, int n)
{
    char str[64],id[16];
    int i;
    
    if (!fp_trace||level>level_trace) return;
    for (i=0;i<n;i++) {
        time2str(obs[i].time,str,3);
        satno2id(obs[i].sat,id);
        fprintf(fp_trace," (%2d) %s %-3s rcv%d %13.3f %13.3f %13.3f %13.3f %d %d %d %d %3.1f %3.1f\n",
              i+1,str,id,obs[i].rcv,obs[i].L[0],obs[i].L[1],obs[i].P[0],
              obs[i].P[1],obs[i].LLI[0],obs[i].LLI[1],obs[i].code[0],
              obs[i].code[1],obs[i].SNR[0]*0.25,obs[i].SNR[1]*0.25);
    }
    fflush(fp_trace);
}
extern void arc_tracenav(int level, const nav_t *nav)
{
    char s1[64],s2[64],id[16];
    int i;
    
    if (!fp_trace||level>level_trace) return;
    for (i=0;i<nav->n;i++) {
        time2str(nav->eph[i].toe,s1,0);
        time2str(nav->eph[i].ttr,s2,0);
        satno2id(nav->eph[i].sat,id);
        fprintf(fp_trace,"(%3d) %-3s : %s %s %3d %3d %02x\n",i+1,
                id,s1,s2,nav->eph[i].iode,nav->eph[i].iodc,nav->eph[i].svh);
    }
    fprintf(fp_trace,"(ion) %9.4e %9.4e %9.4e %9.4e\n",nav->ion_gps[0],
            nav->ion_gps[1],nav->ion_gps[2],nav->ion_gps[3]);
    fprintf(fp_trace,"(ion) %9.4e %9.4e %9.4e %9.4e\n",nav->ion_gps[4],
            nav->ion_gps[5],nav->ion_gps[6],nav->ion_gps[7]);
    fprintf(fp_trace,"(ion) %9.4e %9.4e %9.4e %9.4e\n",nav->ion_gal[0],
            nav->ion_gal[1],nav->ion_gal[2],nav->ion_gal[3]);
}
extern void arc_tracegnav(int level, const nav_t *nav)
{
    char s1[64],s2[64],id[16];
    int i;
    
    if (!fp_trace||level>level_trace) return;
    for (i=0;i<nav->ng;i++) {
        time2str(nav->geph[i].toe,s1,0);
        time2str(nav->geph[i].tof,s2,0);
        satno2id(nav->geph[i].sat,id);
        fprintf(fp_trace,"(%3d) %-3s : %s %s %2d %2d %8.3f\n",i+1,
                id,s1,s2,nav->geph[i].frq,nav->geph[i].svh,nav->geph[i].taun*1E6);
    }
}
extern void arc_tracehnav(int level, const nav_t *nav)
{
    char s1[64],s2[64],id[16];
    int i;
    
    if (!fp_trace||level>level_trace) return;
    for (i=0;i<nav->ns;i++) {
        time2str(nav->seph[i].t0,s1,0);
        time2str(nav->seph[i].tof,s2,0);
        satno2id(nav->seph[i].sat,id);
        fprintf(fp_trace,"(%3d) %-3s : %s %s %2d %2d\n",i+1,
                id,s1,s2,nav->seph[i].svh,nav->seph[i].sva);
    }
}
extern void arc_tracepeph(int level, const nav_t *nav)
{
    char s[64],id[16];
    int i,j;
    
    if (!fp_trace||level>level_trace) return;
    
    for (i=0;i<nav->ne;i++) {
        time2str(nav->peph[i].time,s,0);
        for (j=0;j<MAXSAT;j++) {
            satno2id(j+1,id);
            fprintf(fp_trace,"%-3s %d %-3s %13.3f %13.3f %13.3f %13.3f %6.3f %6.3f %6.3f %6.3f\n",
                    s,nav->peph[i].index,id,
                    nav->peph[i].pos[j][0],nav->peph[i].pos[j][1],
                    nav->peph[i].pos[j][2],nav->peph[i].pos[j][3]*1E9,
                    nav->peph[i].std[j][0],nav->peph[i].std[j][1],
                    nav->peph[i].std[j][2],nav->peph[i].std[j][3]*1E9);
        }
    }
}
extern void arc_tracepclk(int level, const nav_t *nav)
{
    char s[64],id[16];
    int i,j;
    
    if (!fp_trace||level>level_trace) return;
    
    for (i=0;i<nav->nc;i++) {
        time2str(nav->pclk[i].time,s,0);
        for (j=0;j<MAXSAT;j++) {
            satno2id(j+1,id);
            fprintf(fp_trace,"%-3s %d %-3s %13.3f %6.3f\n",
                    s,nav->pclk[i].index,id,
                    nav->pclk[i].clk[j][0]*1E9,nav->pclk[i].std[j][0]*1E9);
        }
    }
}
extern void arc_traceb(int level, const unsigned char *p, int n)
{
    int i;
    if (!fp_trace||level>level_trace) return;
    for (i=0;i<n;i++) fprintf(fp_trace,"%02X%s",*p++,i%8==7?" ":"");
    fprintf(fp_trace,"\n");
}
#else
extern void arc_traceopen(const char *file) {}
extern void traceclose(void) {}
extern void tracelevel(int level) {}
extern void arc_log   (int level, const char *format, ...) {}
extern void tracet  (int level, const char *format, ...) {}
extern void tracemat(int level, const double *A, int n, int m, int p, int q) {}
extern void traceobs(int level, const obsd_t *obs, int n) {}
extern void tracenav(int level, const nav_t *nav) {}
extern void tracegnav(int level, const nav_t *nav) {}
extern void tracehnav(int level, const nav_t *nav) {}
extern void tracepeph(int level, const nav_t *nav) {}
extern void tracepclk(int level, const nav_t *nav) {}
extern void traceb  (int level, const unsigned char *p, int n) {}

#endif /* TRACE */

/* expand file path ------------------------------------------------------------
* expand file path with wild-card (*) in file
* args   : char   *path     I   file path to expand (captal insensitive)
*          char   *paths    O   expanded file paths
*          int    nmax      I   max number of expanded file paths
* return : number of expanded file paths
* notes  : the order of expanded files is alphabetical order
*-----------------------------------------------------------------------------*/
extern int expath(const char *path, char *paths[], int nmax)
{
    int i,j,n=0;
    char tmp[1024],path_[1024];
#ifdef WIN32
    WIN32_FIND_DATA file;
    HANDLE h;
    char dir[1024]="",*p;
    
    arc_log(ARC_INFO,"expath  : path=%s nmax=%d\n",path,nmax);
    
	strcpy(path_,path);
    if ((p=strrchr(path_,'\\'))) {
        strncpy(dir,path_,p-path_+1); dir[p-path_+1]='\0';
    }
    if ((h=FindFirstFile((LPCTSTR)path_,&file))==INVALID_HANDLE_VALUE) {
        strcpy(paths[0],path_);
        return 1;
    }
    sprintf(paths[n++],"%s%s",dir,file.cFileName);
    while (FindNextFile(h,&file)&&n<nmax) {
        if (file.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        sprintf(paths[n++],"%s%s",dir,file.cFileName);
    }
    FindClose(h);
#else
    struct dirent *d;
    DIR *dp;
    const char *file=path;
    char dir[1024]="",s1[1024],s2[1024],*p,*q,*r;
    strcpy(path_,path);

    arc_log(ARC_INFO, "expath  : path=%s nmax=%d\n", path, nmax);
    
    if ((p=strrchr(path_,'/'))||(p=strrchr(path_,'\\'))) {
        file=p+1; strncpy(dir,path_,p-path_+1); dir[p-path_+1]='\0';
    }
    if (!(dp=opendir(*dir?dir:"."))) return 0;
    while ((d=readdir(dp))) {
        if (*(d->d_name)=='.') continue;
        sprintf(s1,"^%s$",d->d_name);
        sprintf(s2,"^%s$",file);
        for (p=s1;*p;p++) *p=(char)tolower((int)*p);
        for (p=s2;*p;p++) *p=(char)tolower((int)*p);
        
        for (p=s1,q=strtok_r(s2,"*",&r);q;q=strtok_r(NULL,"*",&r)) {
            if ((p=strstr(p,q))) p+=strlen(q); else break;
        }
        if (p&&n<nmax) sprintf(paths[n++],"%s%s",dir,d->d_name);
    }
    closedir(dp);
#endif
    /* sort paths in alphabetical order */
    for (i=0;i<n-1;i++) {
        for (j=i+1;j<n;j++) {
            if (strcmp(paths[i],paths[j])>0) {
                strcpy(tmp,paths[i]);
                strcpy(paths[i],paths[j]);
                strcpy(paths[j],tmp);
            }
        }
    }
    for (i=0;i<n;i++) arc_log(ARC_INFO, "expath  : file=%s\n", paths[i]);
    
    return n;
}
/* replace string ------------------------------------------------------------*/
static int arc_repstr(char *str, const char *pat, const char *rep)
{
    int len=(int)strlen(pat);
    char buff[1024],*p,*q,*r;
    
    for (p=str,r=buff;*p;p=q+len) {
        if (!(q=strstr(p,pat))) break;
        strncpy(r,p,q-p);
        r+=q-p;
        r+=sprintf(r,"%s",rep);
    }
    if (p<=str) return 0;
    strcpy(r,p);
    strcpy(str,buff);
    return 1;
}
/* replace keywords in file path -----------------------------------------------
* replace keywords in file path with date, time, rover and base station id
* args   : char   *path     I   file path (see below)
*          char   *rpath    O   file path in which keywords replaced (see below)
*          gtime_t time     I   time (gpst)  (time.time==0: not replaced)
*          char   *rov      I   rover id string        ("": not replaced)
*          char   *base     I   base station id string ("": not replaced)
* return : status (1:keywords replaced, 0:no valid keyword in the path,
*                  -1:no valid time)
* notes  : the following keywords in path are replaced by date, time and name
*              %Y -> yyyy : year (4 digits) (1900-2099)
*              %y -> yy   : year (2 digits) (00-99)
*              %m -> mm   : month           (01-12)
*              %d -> dd   : day of month    (01-31)
*              %h -> hh   : hours           (00-23)
*              %M -> mm   : minutes         (00-59)
*              %S -> ss   : seconds         (00-59)
*              %n -> ddd  : day of year     (001-366)
*              %W -> wwww : gps week        (0001-9999)
*              %D -> d    : day of gps week (0-6)
*              %H -> h    : hour code       (a=0,b=1,c=2,...,x=23)
*              %ha-> hh   : 3 hours         (00,03,06,...,21)
*              %hb-> hh   : 6 hours         (00,06,12,18)
*              %hc-> hh   : 12 hours        (00,12)
*              %t -> mm   : 15 minutes      (00,15,30,45)
*              %r -> rrrr : rover id
*              %b -> bbbb : base station id
*-----------------------------------------------------------------------------*/
extern int reppath(const char *path, char *rpath, gtime_t time, const char *rov,
                   const char *base)
{
    double ep[6],ep0[6]={2000,1,1,0,0,0};
    int week,dow,doy,stat=0;
    char rep[64];
    
    strcpy(rpath,path);
    
    if (!strstr(rpath,"%")) return 0;
    if (*rov ) stat|=arc_repstr(rpath,"%r",rov );
    if (*base) stat|=arc_repstr(rpath,"%b",base);
    if (time.time!=0) {
        time2epoch(time,ep);
        ep0[0]=ep[0];
        dow=(int)floor(time2gpst(time,&week)/86400.0);
        doy=(int)floor(timediff(time,epoch2time(ep0))/86400.0)+1;
        sprintf(rep,"%02d",  ((int)ep[3]/3)*3);   stat|=arc_repstr(rpath,"%ha",rep);
        sprintf(rep,"%02d",  ((int)ep[3]/6)*6);   stat|=arc_repstr(rpath,"%hb",rep);
        sprintf(rep,"%02d",  ((int)ep[3]/12)*12); stat|=arc_repstr(rpath,"%hc",rep);
        sprintf(rep,"%04.0f",ep[0]);              stat|=arc_repstr(rpath,"%Y",rep);
        sprintf(rep,"%02.0f",fmod(ep[0],100.0));  stat|=arc_repstr(rpath,"%y",rep);
        sprintf(rep,"%02.0f",ep[1]);              stat|=arc_repstr(rpath,"%m",rep);
        sprintf(rep,"%02.0f",ep[2]);              stat|=arc_repstr(rpath,"%d",rep);
        sprintf(rep,"%02.0f",ep[3]);              stat|=arc_repstr(rpath,"%h",rep);
        sprintf(rep,"%02.0f",ep[4]);              stat|=arc_repstr(rpath,"%M",rep);
        sprintf(rep,"%02.0f",floor(ep[5]));       stat|=arc_repstr(rpath,"%S",rep);
        sprintf(rep,"%03d",  doy);                stat|=arc_repstr(rpath,"%n",rep);
        sprintf(rep,"%04d",  week);               stat|=arc_repstr(rpath,"%W",rep);
        sprintf(rep,"%d",    dow);                stat|=arc_repstr(rpath,"%D",rep);
        sprintf(rep,"%c",    'a'+(int)ep[3]);     stat|=arc_repstr(rpath,"%H",rep);
        sprintf(rep,"%02d",  ((int)ep[4]/15)*15); stat|=arc_repstr(rpath,"%t",rep);
    }
    else if (strstr(rpath,"%ha")||strstr(rpath,"%hb")||strstr(rpath,"%hc")||
             strstr(rpath,"%Y" )||strstr(rpath,"%y" )||strstr(rpath,"%m" )||
             strstr(rpath,"%d" )||strstr(rpath,"%h" )||strstr(rpath,"%M" )||
             strstr(rpath,"%S" )||strstr(rpath,"%n" )||strstr(rpath,"%W" )||
             strstr(rpath,"%D" )||strstr(rpath,"%H" )||strstr(rpath,"%t" )) {
        return -1; /* no valid time */
    }
    return stat;
}
/* replace keywords in file path and generate multiple paths -------------------
* replace keywords in file path with date, time, rover and base station id
* generate multiple keywords-replaced paths
* args   : char   *path     I   file path (see below)
*          char   *rpath[]  O   file paths in which keywords replaced
*          int    nmax      I   max number of output file paths
*          gtime_t ts       I   time start (gpst)
*          gtime_t te       I   time end   (gpst)
*          char   *rov      I   rover id string        ("": not replaced)
*          char   *base     I   base station id string ("": not replaced)
* return : number of replaced file paths
* notes  : see reppath() for replacements of keywords.
*          minimum interval of time replaced is 900s.
*-----------------------------------------------------------------------------*/
extern int reppaths(const char *path, char *rpath[], int nmax, gtime_t ts,
                    gtime_t te, const char *rov, const char *base)
{
    gtime_t time;
    double tow,tint=86400.0;
    int i,n=0,week;

    arc_log(ARC_INFO, "reppaths: path =%s nmax=%d rov=%s base=%s\n", path, nmax, rov, base);
    
    if (ts.time==0||te.time==0||timediff(ts,te)>0.0) return 0;
    
    if (strstr(path,"%S")||strstr(path,"%M")||strstr(path,"%t")) tint=900.0;
    else if (strstr(path,"%h")||strstr(path,"%H")) tint=3600.0;
    
    tow=time2gpst(ts,&week);
    time=gpst2time(week,floor(tow/tint)*tint);
    
    while (timediff(time,te)<=0.0&&n<nmax) {
        reppath(path,rpath[n],time,rov,base);
        if (n==0||strcmp(rpath[n],rpath[n-1])) n++;
        time=timeadd(time,tint);
    }
    for (i=0;i<n;i++) arc_log(3, "reppaths: rpath=%s\n", rpath[i]);
    return n;
}
/* satellite carrier wave length -----------------------------------------------
* get satellite carrier wave lengths
* args   : int    sat       I   satellite number
*          int    frq       I   frequency index (0:L1,1:L2,2:L5/3,...)
*          nav_t  *nav      I   navigation messages
* return : carrier wave length (m) (0.0: error)
*-----------------------------------------------------------------------------*/
extern double arc_satwavelen(int sat, int frq, const nav_t *nav)
{
    const double freq_glo[]={FREQ1_GLO,FREQ2_GLO};
    const double dfrq_glo[]={DFRQ1_GLO,DFRQ2_GLO};
    int i,sys=satsys(sat,NULL);
    
    if (sys==SYS_GLO) {
        if (0<=frq&&frq<=1) {
            for (i=0;i<nav->ng;i++) {
                if (nav->geph[i].sat!=sat) continue;
                return CLIGHT/(freq_glo[frq]+dfrq_glo[frq]*nav->geph[i].frq);
            }
        }
        else if (frq==2) { /* L3 */
            return CLIGHT/FREQ3_GLO;
        }
    }
    else if (sys==SYS_CMP) {
        if      (frq==0) return CLIGHT/FREQ1_CMP; /* B1 */
        else if (frq==1) return CLIGHT/FREQ2_CMP; /* B2 */
        else if (frq==2) return CLIGHT/FREQ3_CMP; /* B3 */
    }
    else {
        if      (frq==0) return CLIGHT/FREQ1; /* L1/E1 */
        else if (frq==1) return CLIGHT/FREQ2; /* L2 */
        else if (frq==2) return CLIGHT/FREQ5; /* L5/E5a */
        else if (frq==3) return CLIGHT/FREQ6; /* L6/LEX */
        else if (frq==4) return CLIGHT/FREQ7; /* E5b */
        else if (frq==5) return CLIGHT/FREQ8; /* E5a+b */
        else if (frq==6) return CLIGHT/FREQ9; /* S */
    }
    return 0.0;
}
/* geometric distance ----------------------------------------------------------
* compute geometric distance and receiver-to-satellite unit vector
* args   : double *rs       I   satellilte position (ecef at transmission) (m)
*          double *rr       I   receiver position (ecef at reception) (m)
*          double *e        O   line-of-sight vector (ecef)
* return : geometric distance (m) (0>:error/no satellite position)
* notes  : distance includes sagnac effect correction
*-----------------------------------------------------------------------------*/
extern double arc_geodist(const double *rs, const double *rr, double *e)
{
    double r;
    int i;
    
    if (arc_norm(rs, 3)<RE_WGS84) return -1.0;
    for (i=0;i<3;i++) e[i]=rs[i]-rr[i];
    r= arc_norm(e, 3);
    for (i=0;i<3;i++) e[i]/=r;
    return r+OMGE*(rs[0]*rr[1]-rs[1]*rr[0])/CLIGHT;
}
/* satellite azimuth/elevation angle -------------------------------------------
* compute satellite azimuth/elevation angle
* args   : double *pos      I   geodetic position {lat,lon,h} (rad,m)
*          double *e        I   receiver-to-satellilte unit vevtor (ecef)
*          double *azel     IO  azimuth/elevation {az,el} (rad) (NULL: no output)
*                               (0.0<=azel[0]<2*pi,-pi/2<=azel[1]<=pi/2)
* return : elevation angle (rad)
*-----------------------------------------------------------------------------*/
extern double arc_satazel(const double *pos, const double *e, double *azel)
{
    double az=0.0,el=PI/2.0,enu[3];
    
    if (pos[2]>-RE_WGS84) {
        ecef2enu(pos,e,enu);
        az= arc_dot(enu, enu, 2)<1E-12?0.0:atan2(enu[0],enu[1]);
        if (az<0.0) az+=2*PI;
        el=asin(enu[2]);
    }
    if (azel) {azel[0]=az; azel[1]=el;}
    return el;
}
/* compute dops ----------------------------------------------------------------
* compute DOP (dilution of precision)
* args   : int    ns        I   number of satellites
*          double *azel     I   satellite azimuth/elevation angle (rad)
*          double elmin     I   elevation cutoff angle (rad)
*          double *dop      O   DOPs {GDOP,PDOP,HDOP,VDOP}
* return : none
* notes  : dop[0]-[3] return 0 in case of dop computation error
*-----------------------------------------------------------------------------*/
#define SQRT(x)     ((x)<0.0?0.0:sqrt(x))

extern void arc_dops(int ns, const double *azel, double elmin, double *dop)
{
    double H[4*MAXSAT],Q[16],cosel,sinel;
    int i,n;
    
    for (i=0;i<4;i++) dop[i]=0.0;
    for (i=n=0;i<ns&&i<MAXSAT;i++) {
        if (azel[1+i*2]<elmin||azel[1+i*2]<=0.0) continue;
        cosel=cos(azel[1+i*2]);
        sinel=sin(azel[1+i*2]);
        H[  4*n]=cosel*sin(azel[i*2]);
        H[1+4*n]=cosel*cos(azel[i*2]);
        H[2+4*n]=sinel;
        H[3+4*n++]=1.0;
    }
    if (n<4) return;

    arc_matmul("NT",4,4,n,1.0,H,H,0.0,Q);
    if (!arc_matinv(Q,4)) {
        dop[0]=SQRT(Q[0]+Q[5]+Q[10]+Q[15]); /* GDOP */
        dop[1]=SQRT(Q[0]+Q[5]+Q[10]);       /* PDOP */
        dop[2]=SQRT(Q[0]+Q[5]);             /* HDOP */
        dop[3]=SQRT(Q[10]);                 /* VDOP */
    }
}
/* ionosphere model ------------------------------------------------------------
* compute ionospheric delay by broadcast ionosphere model (klobuchar model)
* args   : gtime_t t        I   time (gpst)
*          double *ion      I   iono model parameters {a0,a1,a2,a3,b0,b1,b2,b3}
*          double *pos      I   receiver position {lat,lon,h} (rad,m)
*          double *azel     I   azimuth/elevation angle {az,el} (rad)
* return : ionospheric delay (L1) (m)
*-----------------------------------------------------------------------------*/
extern double arc_ionmodel(gtime_t t, const double *ion, const double *pos,
                           const double *azel)
{
    const double ion_default[]={ /* 2004/1/1 */
        0.1118E-07,-0.7451E-08,-0.5961E-07, 0.1192E-06,
        0.1167E+06,-0.2294E+06,-0.1311E+06, 0.1049E+07
    };
    double tt,f,psi,phi,lam,amp,per,x;
    int week;
    
    if (pos[2]<-1E3||azel[1]<=0) return 0.0;
    if (arc_norm(ion, 8)<=0.0) ion=ion_default;
    
    /* earth centered angle (semi-circle) */
    psi=0.0137/(azel[1]/PI+0.11)-0.022;
    
    /* subionospheric latitude/longitude (semi-circle) */
    phi=pos[0]/PI+psi*cos(azel[0]);
    if      (phi> 0.416) phi= 0.416;
    else if (phi<-0.416) phi=-0.416;
    lam=pos[1]/PI+psi*sin(azel[0])/cos(phi*PI);
    
    /* geomagnetic latitude (semi-circle) */
    phi+=0.064*cos((lam-1.617)*PI);
    
    /* local time (s) */
    tt=43200.0*lam+time2gpst(t,&week);
    tt-=floor(tt/86400.0)*86400.0; /* 0<=tt<86400 */
    
    /* slant factor */
    f=1.0+16.0*pow(0.53-azel[1]/PI,3.0);
    
    /* ionospheric delay */
    amp=ion[0]+phi*(ion[1]+phi*(ion[2]+phi*ion[3]));
    per=ion[4]+phi*(ion[5]+phi*(ion[6]+phi*ion[7]));
    amp=amp<    0.0?    0.0:amp;
    per=per<72000.0?72000.0:per;
    x=2.0*PI*(tt-50400.0)/per;
    
    return CLIGHT*f*(fabs(x)<1.57?5E-9+amp*(1.0+x*x*(-0.5+x*x/24.0)):5E-9);
}
/* ionosphere mapping function -------------------------------------------------
* compute ionospheric delay mapping function by single layer model
* args   : double *pos      I   receiver position {lat,lon,h} (rad,m)
*          double *azel     I   azimuth/elevation angle {az,el} (rad)
* return : ionospheric mapping function
*-----------------------------------------------------------------------------*/
extern double arc_ionmapf(const double *pos, const double *azel)
{
    if (pos[2]>=HION) return 1.0;
    return 1.0/cos(asin((RE_WGS84+pos[2])/(RE_WGS84+HION)*sin(PI/2.0-azel[1])));
}
/* ionospheric pierce point position -------------------------------------------
* compute ionospheric pierce point (ipp) position and slant factor
* args   : double *pos      I   receiver position {lat,lon,h} (rad,m)
*          double *azel     I   azimuth/elevation angle {az,el} (rad)
*          double re        I   earth radius (km)
*          double hion      I   altitude of ionosphere (km)
*          double *posp     O   pierce point position {lat,lon,h} (rad,m)
* return : slant factor
* notes  : see ref [2], only valid on the earth surface
*          fixing bug on ref [2] A.4.4.10.1 A-22,23
*-----------------------------------------------------------------------------*/
extern double arc_ionppp(const double *pos, const double *azel, double re,
                         double hion, double *posp)
{
    double cosaz,rp,ap,sinap,tanap;
    
    rp=re/(re+hion)*cos(azel[1]);
    ap=PI/2.0-azel[1]-asin(rp);
    sinap=sin(ap);
    tanap=tan(ap);
    cosaz=cos(azel[0]);
    posp[0]=asin(sin(pos[0])*cos(ap)+cos(pos[0])*sinap*cosaz);
    
    if ((pos[0]> 70.0*D2R&& tanap*cosaz>tan(PI/2.0-pos[0]))||
        (pos[0]<-70.0*D2R&&-tanap*cosaz>tan(PI/2.0+pos[0]))) {
        posp[1]=pos[1]+PI-asin(sinap*sin(azel[0])/cos(posp[0]));
    }
    else {
        posp[1]=pos[1]+asin(sinap*sin(azel[0])/cos(posp[0]));
    }
    return 1.0/sqrt(1.0-rp*rp);
}
/* troposphere model -----------------------------------------------------------
* compute tropospheric delay by standard atmosphere and saastamoinen model
* args   : gtime_t time     I   time
*          double *pos      I   receiver position {lat,lon,h} (rad,m)
*          double *azel     I   azimuth/elevation angle {az,el} (rad)
*          double humi      I   relative humidity
* return : tropospheric delay (m)
*-----------------------------------------------------------------------------*/
extern double arc_tropmodel(gtime_t time, const double *pos, const double *azel,
                            double humi)
{
    const double temp0=15.0; /* temparature at sea level */
    double hgt,pres,temp,e,z,trph,trpw;
    
    if (pos[2]<-100.0||1E4<pos[2]||azel[1]<=0) return 0.0;
    
    /* standard atmosphere */
    hgt=pos[2]<0.0?0.0:pos[2];
    
    pres=1013.25*pow(1.0-2.2557E-5*hgt,5.2568);
    temp=temp0-6.5E-3*hgt+273.16;
    e=6.108*humi*exp((17.15*temp-4684.0)/(temp-38.45));
    
    /* saastamoninen model */
    z=PI/2.0-azel[1];
    trph=0.0022768*pres/(1.0-0.00266*cos(2.0*pos[0])-0.00028*hgt/1E3)/cos(z);
    trpw=0.002277*(1255.0/temp+0.05)*e/cos(z);
    return trph+trpw;
}
#ifndef IERS_MODEL

static double arc_interpc(const double coef[], double lat)
{
    int i=(int)(lat/15.0);
    if (i<1) return coef[0]; else if (i>4) return coef[4];
    return coef[i-1]*(1.0-lat/15.0+i)+coef[i]*(lat/15.0-i);
}
static double arc_mapf(double el, double a, double b, double c)
{
    double sinel=sin(el);
    return (1.0+a/(1.0+b/(1.0+c)))/(sinel+(a/(sinel+b/(sinel+c))));
}
static double arc_nmf(gtime_t time, const double pos[], const double azel[],
                      double *mapfw)
{
    /* ref [5] table 3 */
    /* hydro-ave-a,b,c, hydro-amp-a,b,c, wet-a,b,c at latitude 15,30,45,60,75 */
    const double coef[][5]={
        { 1.2769934E-3, 1.2683230E-3, 1.2465397E-3, 1.2196049E-3, 1.2045996E-3},
        { 2.9153695E-3, 2.9152299E-3, 2.9288445E-3, 2.9022565E-3, 2.9024912E-3},
        { 62.610505E-3, 62.837393E-3, 63.721774E-3, 63.824265E-3, 64.258455E-3},
        
        { 0.0000000E-0, 1.2709626E-5, 2.6523662E-5, 3.4000452E-5, 4.1202191E-5},
        { 0.0000000E-0, 2.1414979E-5, 3.0160779E-5, 7.2562722E-5, 11.723375E-5},
        { 0.0000000E-0, 9.0128400E-5, 4.3497037E-5, 84.795348E-5, 170.37206E-5},
        
        { 5.8021897E-4, 5.6794847E-4, 5.8118019E-4, 5.9727542E-4, 6.1641693E-4},
        { 1.4275268E-3, 1.5138625E-3, 1.4572752E-3, 1.5007428E-3, 1.7599082E-3},
        { 4.3472961E-2, 4.6729510E-2, 4.3908931E-2, 4.4626982E-2, 5.4736038E-2}
    };
    const double aht[]={ 2.53E-5, 5.49E-3, 1.14E-3}; /* height correction */
    
    double y,cosy,ah[3],aw[3],dm,el=azel[1],lat=pos[0]*R2D,hgt=pos[2];
    int i;
    
    if (el<=0.0) {
        if (mapfw) *mapfw=0.0;
        return 0.0;
    }
    /* year from doy 28, added half a year for southern latitudes */
    y=(time2doy(time)-28.0)/365.25+(lat<0.0?0.5:0.0);
    
    cosy=cos(2.0*PI*y);
    lat=fabs(lat);
    
    for (i=0;i<3;i++) {
        ah[i]=arc_interpc(coef[i  ],lat)-arc_interpc(coef[i+3],lat)*cosy;
        aw[i]=arc_interpc(coef[i+6],lat);
    }
    /* ellipsoidal height is used instead of height above sea level */
    dm=(1.0/sin(el)-arc_mapf(el,aht[0],aht[1],aht[2]))*hgt/1E3;
    
    if (mapfw) *mapfw=arc_mapf(el,aw[0],aw[1],aw[2]);
    
    return arc_mapf(el,ah[0],ah[1],ah[2])+dm;
}
#endif /* !IERS_MODEL */

/* troposphere mapping function ------------------------------------------------
* compute tropospheric mapping function by NMF
* args   : gtime_t t        I   time
*          double *pos      I   receiver position {lat,lon,h} (rad,m)
*          double *azel     I   azimuth/elevation angle {az,el} (rad)
*          double *mapfw    IO  wet mapping function (NULL: not output)
* return : dry mapping function
* note   : see ref [5] (NMF) and [9] (GMF)
*          original JGR paper of [5] has bugs in eq.(4) and (5). the corrected
*          paper is obtained from:
*          ftp://web.haystack.edu/pub/aen/nmf/NMF_JGR.pdf
*-----------------------------------------------------------------------------*/
extern double arc_tropmapf(gtime_t time, const double pos[], const double azel[],
                           double *mapfw)
{
#ifdef IERS_MODEL
    const double ep[]={2000,1,1,12,0,0};
    double mjd,lat,lon,hgt,zd,gmfh,gmfw;
#endif
    arc_log(ARC_INFO, "tropmapf: pos=%10.6f %11.6f %6.1f azel=%5.1f %4.1f\n",
            pos[0] * R2D, pos[1] * R2D, pos[2], azel[0] * R2D, azel[1] * R2D);
    
    if (pos[2]<-1000.0||pos[2]>20000.0) {
        if (mapfw) *mapfw=0.0;
        return 0.0;
    }
#ifdef IERS_MODEL
    mjd=51544.5+(timediff(time,epoch2time(ep)))/86400.0;
    lat=pos[0];
    lon=pos[1];
    hgt=pos[2]-geoidh(pos); /* height in m (mean sea level) */
    zd =PI/2.0-azel[1];
    
    /* call GMF */
    gmf_(&mjd,&lat,&lon,&hgt,&zd,&gmfh,&gmfw);
    
    if (mapfw) *mapfw=gmfw;
    return gmfh;
#else
    return arc_nmf(time,pos,azel,mapfw); /* NMF */
#endif
}
/* interpolate antenna phase center variation --------------------------------*/
static double arc_interpvar(double ang, const double *var)
{
    double a=ang/5.0; /* ang=0-90 */
    int i=(int)a;
    if (i<0) return var[0]; else if (i>=18) return var[18];
    return var[i]*(1.0-a+i)+var[i+1]*(a-i);
}
/* receiver antenna model ------------------------------------------------------
* compute antenna offset by antenna phase center parameters
* args   : pcv_t *pcv       I   antenna phase center parameters
*          double *azel     I   azimuth/elevation for receiver {az,el} (rad)
*          int     opt      I   option (0:only offset,1:offset+pcv)
*          double *dant     O   range offsets for each frequency (m)
* return : none
* notes  : current version does not support azimuth dependent terms
*-----------------------------------------------------------------------------*/
extern void arc_antmodel(const pcv_t *pcv, const double *del, const double *azel,
                         int opt, double *dant)
{
    double e[3],off[3],cosel=cos(azel[1]);
    int i,j;

    arc_log(ARC_INFO, "antmodel: azel=%6.1f %4.1f opt=%d\n", azel[0]*R2D,azel[1]*R2D,opt);
    
    e[0]=sin(azel[0])*cosel;
    e[1]=cos(azel[0])*cosel;
    e[2]=sin(azel[1]);
    
    for (i=0;i<NFREQ;i++) {
        for (j=0;j<3;j++) off[j]=pcv->off[i][j]+del[j];
        
        dant[i]=-arc_dot(off,e,3)+(opt?arc_interpvar(90.0-azel[1]*R2D,pcv->var[i]):0.0);
    }
    arc_log(ARC_INFO, "antmodel: dant=%6.3f %6.3f\n",dant[0],dant[1]);
}
/* satellite antenna model ------------------------------------------------------
* compute satellite antenna phase center parameters
* args   : pcv_t *pcv       I   antenna phase center parameters
*          double nadir     I   nadir angle for satellite (rad)
*          double *dant     O   range offsets for each frequency (m)
* return : none
*-----------------------------------------------------------------------------*/
extern void arc_antmodel_s(const pcv_t *pcv, double nadir, double *dant)
{
    int i;

    arc_log(ARC_INFO, "antmodel_s: nadir=%6.1f\n", nadir * R2D);
    
    for (i=0;i<NFREQ;i++) {
        dant[i]=arc_interpvar(nadir*R2D*5.0,pcv->var[i]);
    }
    arc_log(ARC_INFO, "antmodel_s: dant=%6.3f %6.3f\n", dant[0], dant[1]);
}
/* sun and moon position in eci (ref [4] 5.1.1, 5.2.1) -----------------------*/
static void sunmoonpos_eci(gtime_t tut, double *rsun, double *rmoon)
{
    const double ep2000[]={2000,1,1,12,0,0};
    double t,f[5],eps,Ms,ls,rs,lm,pm,rm,sine,cose,sinp,cosp,sinl,cosl;

    arc_log(ARC_INFO, "sunmoonpos_eci: tut=%s\n", time_str(tut, 3));
    
    t=timediff(tut,epoch2time(ep2000))/86400.0/36525.0;
    
    /* astronomical arguments */
    ast_args(t,f);
    
    /* obliquity of the ecliptic */
    eps=23.439291-0.0130042*t;
    sine=sin(eps*D2R); cose=cos(eps*D2R);
    
    /* sun position in eci */
    if (rsun) {
        Ms=357.5277233+35999.05034*t;
        ls=280.460+36000.770*t+1.914666471*sin(Ms*D2R)+0.019994643*sin(2.0*Ms*D2R);
        rs=AU*(1.000140612-0.016708617*cos(Ms*D2R)-0.000139589*cos(2.0*Ms*D2R));
        sinl=sin(ls*D2R); cosl=cos(ls*D2R);
        rsun[0]=rs*cosl;
        rsun[1]=rs*cose*sinl;
        rsun[2]=rs*sine*sinl;

        arc_log(ARC_INFO, "rsun =%.3f %.3f %.3f\n", rsun[0], rsun[1], rsun[2]);
    }
    /* moon position in eci */
    if (rmoon) {
        lm=218.32+481267.883*t+6.29*sin(f[0])-1.27*sin(f[0]-2.0*f[3])+
           0.66*sin(2.0*f[3])+0.21*sin(2.0*f[0])-0.19*sin(f[1])-0.11*sin(2.0*f[2]);
        pm=5.13*sin(f[2])+0.28*sin(f[0]+f[2])-0.28*sin(f[2]-f[0])-
           0.17*sin(f[2]-2.0*f[3]);
        rm=RE_WGS84/sin((0.9508+0.0518*cos(f[0])+0.0095*cos(f[0]-2.0*f[3])+
                   0.0078*cos(2.0*f[3])+0.0028*cos(2.0*f[0]))*D2R);
        sinl=sin(lm*D2R); cosl=cos(lm*D2R);
        sinp=sin(pm*D2R); cosp=cos(pm*D2R);
        rmoon[0]=rm*cosp*cosl;
        rmoon[1]=rm*(cose*cosp*sinl-sine*sinp);
        rmoon[2]=rm*(sine*cosp*sinl+cose*sinp);

        arc_log(ARC_INFO, "rmoon=%.3f %.3f %.3f\n", rmoon[0], rmoon[1], rmoon[2]);
    }
}
/* sun and moon position -------------------------------------------------------
* get sun and moon position in ecef
* args   : gtime_t tut      I   time in ut1
*          double *erpv     I   erp value {xp,yp,ut1_utc,lod} (rad,rad,s,s/d)
*          double *rsun     IO  sun position in ecef  (m) (NULL: not output)
*          double *rmoon    IO  moon position in ecef (m) (NULL: not output)
*          double *gmst     O   gmst (rad)
* return : none
*-----------------------------------------------------------------------------*/
extern void arc_sunmoonpos(gtime_t tutc, const double *erpv, double *rsun,
                           double *rmoon, double *gmst)
{
    gtime_t tut;
    double rs[3],rm[3],U[9],gmst_;

    arc_log(ARC_INFO, "sunmoonpos: tutc=%s\n", time_str(tutc, 3));
    
    tut=timeadd(tutc,erpv[2]); /* utc -> ut1 */
    
    /* sun and moon position in eci */
    sunmoonpos_eci(tut,rsun?rs:NULL,rmoon?rm:NULL);
    
    /* eci to ecef transformation matrix */
    eci2ecef(tutc,erpv,U,&gmst_);
    
    /* sun and moon postion in ecef */
    if (rsun ) arc_matmul("NN", 3, 1, 3, 1.0, U, rs, 0.0, rsun);
    if (rmoon) arc_matmul("NN", 3, 1, 3, 1.0, U, rm, 0.0, rmoon);
    if (gmst ) *gmst=gmst_;
}
/* carrier smoothing -----------------------------------------------------------
* carrier smoothing by Hatch filter
* args   : obs_t  *obs      IO  raw observation data/smoothed observation data
*          int    ns        I   smoothing window size (epochs)
* return : none
*-----------------------------------------------------------------------------*/
extern void arc_csmooth(obs_t *obs, int ns)
{
    double Ps[2][MAXSAT][NFREQ]={{{0}}},Lp[2][MAXSAT][NFREQ]={{{0}}},dcp;
    int i,j,s,r,n[2][MAXSAT][NFREQ]={{{0}}};
    obsd_t *p;

    arc_log(ARC_INFO, "csmooth: nobs=%d,ns=%d\n", obs->n, ns);
    
    for (i=0;i<obs->n;i++) {
        p=&obs->data[i]; s=p->sat; r=p->rcv;
        for (j=0;j<NFREQ;j++) {
            if (s<=0||MAXSAT<s||r<=0||2<r) continue;
            if (p->P[j]==0.0||p->L[j]==0.0) continue;
            if (p->LLI[j]) n[r-1][s-1][j]=0;
            if (n[r-1][s-1][j]==0) Ps[r-1][s-1][j]=p->P[j];
            else {
                dcp=lam_carr[j]*(p->L[j]-Lp[r-1][s-1][j]);
                Ps[r-1][s-1][j]=p->P[j]/ns+(Ps[r-1][s-1][j]+dcp)*(ns-1)/ns;
            }
            if (++n[r-1][s-1][j]<ns) p->P[j]=0.0; else p->P[j]=Ps[r-1][s-1][j];
            Lp[r-1][s-1][j]=p->L[j];
        }
    }
}
/* solar/lunar tides (ref [2] 7) ---------------------------------------------*/
#ifndef IERS_MODEL
static void arc_tide_pl(const double *eu, const double *rp, double GMp,
                        const double *pos, double *dr)
{
    const double H3=0.292,L3=0.015;
    double r,ep[3],latp,lonp,p,K2,K3,a,H2,L2,dp,du,cosp,sinl,cosl;
    int i;

    arc_log(ARC_INFO, "tide_pl : pos=%.3f %.3f\n", pos[0] * R2D, pos[1] * R2D);
    
    if ((r= arc_norm(rp, 3))<=0.0) return;
    
    for (i=0;i<3;i++) ep[i]=rp[i]/r;
    
    K2=GMp/GME*SQR(RE_WGS84)*SQR(RE_WGS84)/(r*r*r);
    K3=K2*RE_WGS84/r;
    latp=asin(ep[2]); lonp=atan2(ep[1],ep[0]);
    cosp=cos(latp); sinl=sin(pos[0]); cosl=cos(pos[0]);
    
    /* step1 in phase (degree 2) */
    p=(3.0*sinl*sinl-1.0)/2.0;
    H2=0.6078-0.0006*p;
    L2=0.0847+0.0002*p;
    a= arc_dot(ep, eu, 3);
    dp=K2*3.0*L2*a;
    du=K2*(H2*(1.5*a*a-0.5)-3.0*L2*a*a);
    
    /* step1 in phase (degree 3) */
    dp+=K3*L3*(7.5*a*a-1.5);
    du+=K3*(H3*(2.5*a*a*a-1.5*a)-L3*(7.5*a*a-1.5)*a);
    
    /* step1 out-of-phase (only radial) */
    du+=3.0/4.0*0.0025*K2*sin(2.0*latp)*sin(2.0*pos[0])*sin(pos[1]-lonp);
    du+=3.0/4.0*0.0022*K2*cosp*cosp*cosl*cosl*sin(2.0*(pos[1]-lonp));
    
    dr[0]=dp*ep[0]+du*eu[0];
    dr[1]=dp*ep[1]+du*eu[1];
    dr[2]=dp*ep[2]+du*eu[2];

    arc_log(ARC_INFO, "tide_pl : dr=%.3f %.3f %.3f\n", dr[0], dr[1], dr[2]);
}
/* displacement by solid earth tide (ref [2] 7) ------------------------------*/
static void arc_tide_solid(const double *rsun, const double *rmoon,
                           const double *pos, const double *E, double gmst, int opt,
                           double *dr)
{
    double dr1[3],dr2[3],eu[3],du,dn,sinl,sin2l;

    arc_log(ARC_INFO, "tide_solid: pos=%.3f %.3f opt=%d\n", pos[0] * R2D, pos[1] * R2D, opt);
    
    /* step1: time domain */
    eu[0]=E[2]; eu[1]=E[5]; eu[2]=E[8];
    arc_tide_pl(eu,rsun, GMS,pos,dr1);
    arc_tide_pl(eu,rmoon,GMM,pos,dr2);
    
    /* step2: frequency domain, only K1 radial */
    sin2l=sin(2.0*pos[0]);
    du=-0.012*sin2l*sin(gmst+pos[1]);
    
    dr[0]=dr1[0]+dr2[0]+du*E[2];
    dr[1]=dr1[1]+dr2[1]+du*E[5];
    dr[2]=dr1[2]+dr2[2]+du*E[8];
    
    /* eliminate permanent deformation */
    if (opt&8) {
        sinl=sin(pos[0]); 
        du=0.1196*(1.5*sinl*sinl-0.5);
        dn=0.0247*sin2l;
        dr[0]+=du*E[2]+dn*E[1];
        dr[1]+=du*E[5]+dn*E[4];
        dr[2]+=du*E[8]+dn*E[7];
    }
    arc_log(ARC_INFO, "tide_solid: dr=%.3f %.3f %.3f\n", dr[0], dr[1], dr[2]);
}
#endif /* !IERS_MODEL */

/* displacement by ocean tide loading (ref [2] 7) ----------------------------*/
static void arc_tide_oload(gtime_t tut, const double *odisp, double *denu)
{
    const double args[][5]={
        {1.40519E-4, 2.0,-2.0, 0.0, 0.00},  /* M2 */
        {1.45444E-4, 0.0, 0.0, 0.0, 0.00},  /* S2 */
        {1.37880E-4, 2.0,-3.0, 1.0, 0.00},  /* N2 */
        {1.45842E-4, 2.0, 0.0, 0.0, 0.00},  /* K2 */
        {0.72921E-4, 1.0, 0.0, 0.0, 0.25},  /* K1 */
        {0.67598E-4, 1.0,-2.0, 0.0,-0.25},  /* O1 */
        {0.72523E-4,-1.0, 0.0, 0.0,-0.25},  /* P1 */
        {0.64959E-4, 1.0,-3.0, 1.0,-0.25},  /* Q1 */
        {0.53234E-5, 0.0, 2.0, 0.0, 0.00},  /* Mf */
        {0.26392E-5, 0.0, 1.0,-1.0, 0.00},  /* Mm */
        {0.03982E-5, 2.0, 0.0, 0.0, 0.00}   /* Ssa */
    };
    const double ep1975[]={1975,1,1,0,0,0};
    double ep[6],fday,days,t,t2,t3,a[5],ang,dp[3]={0};
    int i,j;

    arc_log(ARC_INFO, "tide_oload:\n");
    
    /* angular argument: see subroutine arg.f for reference [1] */
    time2epoch(tut,ep);
    fday=ep[3]*3600.0+ep[4]*60.0+ep[5];
    ep[3]=ep[4]=ep[5]=0.0;
    days=timediff(epoch2time(ep),epoch2time(ep1975))/86400.0+1.0;
    t=(27392.500528+1.000000035*days)/36525.0;
    t2=t*t; t3=t2*t;
    
    a[0]=fday;
    a[1]=(279.69668+36000.768930485*t+3.03E-4*t2)*D2R; /* H0 */
    a[2]=(270.434358+481267.88314137*t-0.001133*t2+1.9E-6*t3)*D2R; /* S0 */
    a[3]=(334.329653+4069.0340329577*t-0.010325*t2-1.2E-5*t3)*D2R; /* P0 */
    a[4]=2.0*PI;
    
    /* displacements by 11 constituents */
    for (i=0;i<11;i++) {
        ang=0.0;
        for (j=0;j<5;j++) ang+=a[j]*args[i][j];
        for (j=0;j<3;j++) dp[j]+=odisp[j+i*6]*cos(ang-odisp[j+3+i*6]*D2R);
    }
    denu[0]=-dp[1];
    denu[1]=-dp[2];
    denu[2]= dp[0];

    arc_log(ARC_INFO, "tide_oload: denu=%.3f %.3f %.3f\n", denu[0], denu[1], denu[2]);
}
/* iers mean pole (ref [7] eq.7.25) ------------------------------------------*/
static void arc_iers_mean_pole(gtime_t tut, double *xp_bar, double *yp_bar)
{
    const double ep2000[]={2000,1,1,0,0,0};
    double y,y2,y3;
    
    y=timediff(tut,epoch2time(ep2000))/86400.0/365.25;
    
    if (y<3653.0/365.25) { /* until 2010.0 */
        y2=y*y; y3=y2*y;
        *xp_bar= 55.974+1.8243*y+0.18413*y2+0.007024*y3; /* (mas) */
        *yp_bar=346.346+1.7896*y-0.10729*y2-0.000908*y3;
    }
    else { /* after 2010.0 */
        *xp_bar= 23.513+7.6141*y; /* (mas) */
        *yp_bar=358.891-0.6287*y;
    }
}
/* displacement by pole tide (ref [7] eq.7.26) --------------------------------*/
static void arc_tide_pole(gtime_t tut, const double *pos, const double *erpv,
                          double *denu)
{
    double xp_bar,yp_bar,m1,m2,cosl,sinl;

    arc_log(ARC_INFO, "tide_pole: pos=%.3f %.3f\n", pos[0] * R2D, pos[1] * R2D);
    
    /* iers mean pole (mas) */
    arc_iers_mean_pole(tut,&xp_bar,&yp_bar);
    
    /* ref [7] eq.7.24 */
    m1= erpv[0]/AS2R-xp_bar*1E-3; /* (as) */
    m2=-erpv[1]/AS2R+yp_bar*1E-3;
    
    /* sin(2*theta) = sin(2*phi), cos(2*theta)=-cos(2*phi) */
    cosl=cos(pos[1]);
    sinl=sin(pos[1]);
    denu[0]=  9E-3*sin(pos[0])    *(m1*sinl-m2*cosl); /* de= Slambda (m) */
    denu[1]= -9E-3*cos(2.0*pos[0])*(m1*cosl+m2*sinl); /* dn=-Stheta  (m) */
    denu[2]=-33E-3*sin(2.0*pos[0])*(m1*cosl+m2*sinl); /* du= Sr      (m) */

    arc_log(ARC_INFO, "tide_pole : denu=%.3f %.3f %.3f\n", denu[0], denu[1], denu[2]);
}
/* tidal displacement ----------------------------------------------------------
* displacements by earth tides
* args   : gtime_t tutc     I   time in utc
*          double *rr       I   site position (ecef) (m)
*          int    opt       I   options (or of the followings)
*                                 1: solid earth tide
*                                 2: ocean tide loading
*                                 4: pole tide
*                                 8: elimate permanent deformation
*          double *erp      I   earth rotation parameters (NULL: not used)
*          double *odisp    I   ocean loading parameters  (NULL: not used)
*                                 odisp[0+i*6]: consituent i amplitude radial(m)
*                                 odisp[1+i*6]: consituent i amplitude west  (m)
*                                 odisp[2+i*6]: consituent i amplitude south (m)
*                                 odisp[3+i*6]: consituent i phase radial  (deg)
*                                 odisp[4+i*6]: consituent i phase west    (deg)
*                                 odisp[5+i*6]: consituent i phase south   (deg)
*                                (i=0:M2,1:S2,2:N2,3:K2,4:K1,5:O1,6:P1,7:Q1,
*                                   8:Mf,9:Mm,10:Ssa)
*          double *dr       O   displacement by earth tides (ecef) (m)
* return : none
* notes  : see ref [1], [2] chap 7
*          see ref [4] 5.2.1, 5.2.2, 5.2.3
*          ver.2.4.0 does not use ocean loading and pole tide corrections
*-----------------------------------------------------------------------------*/
extern void arc_tidedisp(gtime_t tutc, const double *rr, int opt, const erp_t *erp,
                         const double *odisp, double *dr)
{
    gtime_t tut;
    double pos[2],E[9],drt[3],denu[3],rs[3],rm[3],gmst,erpv[5]={0};
    int i;
#ifdef IERS_MODEL
    double ep[6],fhr;
    int year,mon,day;
#endif

    arc_log(ARC_INFO, "tidedisp: tutc=%s\n", time_str(tutc, 0));
    
    if (erp) {
        geterp(erp,utc2gpst(tutc),erpv);
    }
    tut=timeadd(tutc,erpv[2]);
    
    dr[0]=dr[1]=dr[2]=0.0;
    
    if (arc_norm(rr, 3)<=0.0) return;
    
    pos[0]=asin(rr[2]/ arc_norm(rr, 3));
    pos[1]=atan2(rr[1],rr[0]);
    xyz2enu(pos,E);
    
    if (opt&1) { /* solid earth tides */
        
        /* sun and moon position in ecef */
        arc_sunmoonpos(tutc, erpv, rs, rm, &gmst);
        
#ifdef IERS_MODEL
        time2epoch(tutc,ep);
        year=(int)ep[0];
        mon =(int)ep[1];
        day =(int)ep[2];
        fhr =ep[3]+ep[4]/60.0+ep[5]/3600.0;
        
        /* call DEHANTTIDEINEL */
        dehanttideinel_((double *)rr,&year,&mon,&day,&fhr,rs,rm,drt);
#else
        arc_tide_solid(rs,rm,pos,E,gmst,opt,drt);
#endif
        for (i=0;i<3;i++) dr[i]+=drt[i];
    }
    if ((opt&2)&&odisp) { /* ocean tide loading */
        arc_tide_oload(tut,odisp,denu);
        arc_matmul("TN", 3, 1, 3, 1.0, E, denu, 0.0, drt);
        for (i=0;i<3;i++) dr[i]+=drt[i];
    }
    if ((opt&4)&&erp) { /* pole tide */
        arc_tide_pole(tut,pos,erpv,denu);
        arc_matmul("TN", 3, 1, 3, 1.0, E, denu, 0.0, drt);
        for (i=0;i<3;i++) dr[i]+=drt[i];
    }
    arc_log(ARC_INFO, "tidedisp: dr=%.3f %.3f %.3f\n", dr[0], dr[1], dr[2]);
}
/* get tick time ---------------------------------------------------------------
* get current tick in ms
* args   : none
* return : current tick in ms
*-----------------------------------------------------------------------------*/
extern unsigned int tickget(void)
{
#ifdef WIN32
    return (unsigned int)timeGetTime();
#else
    struct timespec tp={0};
    struct timeval  tv={0};
    
#ifdef CLOCK_MONOTONIC_RAW
    /* linux kernel > 2.6.28 */
    if (!clock_gettime(CLOCK_MONOTONIC_RAW,&tp)) {
        return tp.tv_sec*1000u+tp.tv_nsec/1000000u;
    }
    else {
        gettimeofday(&tv,NULL);
        return tv.tv_sec*1000u+tv.tv_usec/1000u;
    }
#else
    gettimeofday(&tv,NULL);
    return tv.tv_sec*1000u+tv.tv_usec/1000u;
#endif
#endif /* WIN32 */
}
/* show messages--------------------------------------------------------------*/
extern int arc_showmsg(char *format, ...) {return 0;}
extern void arc_settspan(gtime_t ts, gtime_t te) {}
extern void arc_settime(gtime_t time) {}
/* uncompress file -------------------------------------------------------------
* uncompress (uncompress/unzip/uncompact hatanaka-compression/tar) file
* args   : char   *file     I   input file
*          char   *uncfile  O   uncompressed file
* return : status (-1:error,0:not compressed file,1:uncompress completed)
* note   : creates uncompressed file in tempolary directory
*          gzip and crx2rnx commands have to be installed in commands path
*-----------------------------------------------------------------------------*/
extern int arc_rtk_uncompress(const char *file, char *uncfile)
{
    int stat=0;
    char *p,cmd[2048]="",tmpfile[1024]="",buff[1024],*fname,*dir="";

    arc_log(ARC_INFO, "rtk_uncompress: file=%s\n", file);
    
    strcpy(tmpfile,file);
    if (!(p=strrchr(tmpfile,'.'))) return 0;
    
    /* uncompress by gzip */
    if (!strcmp(p,".z"  )||!strcmp(p,".Z"  )||
        !strcmp(p,".gz" )||!strcmp(p,".GZ" )||
        !strcmp(p,".zip")||!strcmp(p,".ZIP")) {
        
        strcpy(uncfile,tmpfile); uncfile[p-tmpfile]='\0';
        sprintf(cmd,"gzip -f -d -c \"%s\" > \"%s\"",tmpfile,uncfile);
        
        if (execcmd(cmd)) {
            remove(uncfile);
            return -1;
        }
        strcpy(tmpfile,uncfile);
        stat=1;
    }
    /* extract tar file */
    if ((p=strrchr(tmpfile,'.'))&&!strcmp(p,".tar")) {
        
        strcpy(uncfile,tmpfile); uncfile[p-tmpfile]='\0';
        strcpy(buff,tmpfile);
        fname=buff;
#ifdef WIN32
        if ((p=strrchr(buff,'\\'))) {
            *p='\0'; dir=fname; fname=p+1;
        }
        sprintf(cmd,"set PATH=%%CD%%;%%PATH%% & cd /D \"%s\" & tar -xf \"%s\"",
                dir,fname);
#else
        if ((p=strrchr(buff,'/'))) {
            *p='\0'; dir=fname; fname=p+1;
        }
        sprintf(cmd,"tar -C \"%s\" -xf \"%s\"",dir,tmpfile);
#endif
        if (execcmd(cmd)) {
            if (stat) remove(tmpfile);
            return -1;
        }
        if (stat) remove(tmpfile);
        stat=1;
    }
    /* extract hatanaka-compressed file by cnx2rnx */
    else if ((p=strrchr(tmpfile,'.'))&&strlen(p)>3&&(*(p+3)=='d'||*(p+3)=='D')) {
        
        strcpy(uncfile,tmpfile);
        uncfile[p-tmpfile+3]=*(p+3)=='D'?'O':'o';
        sprintf(cmd,"crx2rnx < \"%s\" > \"%s\"",tmpfile,uncfile);
        
        if (execcmd(cmd)) {
            remove(uncfile);
            if (stat) remove(tmpfile);
            return -1;
        }
        if (stat) remove(tmpfile);
        stat=1;
    }
    arc_log(ARC_INFO, "rtk_uncompress: stat=%d\n", stat);
    return stat;
}
/* execute command -------------------------------------------------------------
* execute command line by operating system shell
* args   : char   *cmd      I   command line
* return : execution status (0:ok,0>:error)
*-----------------------------------------------------------------------------*/
extern int execcmd(const char *cmd)
{
#ifdef WIN32
    PROCESS_INFORMATION info;
    STARTUPINFO si={0};
    DWORD stat;
    char cmds[1024];
    
    arc_log(ARC_INFO,"execcmd: cmd=%s\n",cmd);
    
    si.cb=sizeof(si);
    sprintf(cmds,"cmd /c %s",cmd);
    if (!CreateProcess(NULL,(LPTSTR)cmds,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,
                       NULL,&si,&info)) return -1;
    WaitForSingleObject(info.hProcess,INFINITE);
    if (!GetExitCodeProcess(info.hProcess,&stat)) stat=-1;
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    return (int)stat;
#else
    arc_log(3, "execcmd: cmd=%s\n", cmd);
    
    return system(cmd);
#endif
}
/* computethe trace of matrix---------------------------------------------------*/
extern double arc_mattrace(double *A,int n)
{
    int i;
    double trace=0.0;
    if (A==NULL||n<=0) trace=0.0;
    for (i=0;i<n;i++) trace+=A[i*n+i]; return trace;
}
/*----------------------------------------------------------------------------*/
static double arc_K2C(const double K)
{
    return K-273.16;
}
/*----------------------------------------------------------------------------*/
static double arc_C2K(const double C)
{
    return C+273.16;
}
/*----------------------------------------------------------------------------*/
extern double arc_tropmodel_hopf(gtime_t time, const double *pos, const double *azel,
                                 double humi)
{
    const double temp0=15.0; /* temparature at sea level */
    double hgt,pres,temp,e,z,trph=0.0,trpw=0.0,hd,hw,sink=0.0,sinp=0.0;

    if (pos[2]<-100.0||1E4<pos[2]||azel[1]<=0) return 0.0;

    /* standard atmosphere */
    hd=hw=hgt=pos[2]<0.0?0.0:pos[2];

    pres=1013.25*pow(1.0-2.2557E-5*hgt,5.2568);
    temp=temp0-6.5E-3*hgt+273.15;
    e=6.108*humi*exp((17.15*temp-4684.0)/(temp-38.45));

    /* hopfield model */
    hd=40136.0+148.72*(temp-273.16); hw=7508.0+0.002421*exp(temp/22.9);

    sink=sin(SQRT(SQR(azel[1])+(6.25*D2R)));
    sinp=sin(SQRT(SQR(azel[1])+(2.25*D2R)));

    trph=155.2E-7*pres/temp*(hd-hgt)/sink;
    trpw=155.2E-7*4810/SQR(temp)*e*(hw-hgt)/sinp;

    return trph+trpw;
}
/*----------------------------------------------------------------------------*/
static int arc_get_amtf(const double*pos,double*args,gtime_t time)
{
    int ilon=0,i;
    double lat,m,doy;

    if (fabs(pos[0])<=15.0*D2R) {
        for (i=0;i<5;i++) {
            args[i]=amt_avg[0][i+1];
            args[5+i]=(pos[1]<0.0?-1.0:1.0)*amt_amp[0][i+1];
        }
        return 1;
    }

    if (fabs(pos[0])>=75.0*D2R) {
        for (i=0;i<5;i++) {
            args[i]=amt_avg[4][i+1];
            args[5+i]=(pos[1]<0.0?-1.0:1.0)*amt_amp[4][i+1];
        }
        return 1;
    }
    else {
        lat=fabs(pos[0]*R2D); ilon=lat/15.0;
        m=(lat-amt_avg[ilon-1][0])/(amt_avg[ilon][0]-amt_avg[ilon-1][0]);

        doy=time2doy(time)+(pos[0]<0.0?182.625:0.0);

        for (i=0;i<5;i++) {
            args[i]=amt_avg[ilon-1][i+1]+
                    (amt_avg[ilon][i+1]-amt_avg[ilon-1][i+1])*m-
                    (amt_amp[ilon-1][i+1]+(amt_avg[ilon][i+1]-
                                           amt_avg[ilon-1][i+1])*m)*cos(2*PI*(doy-28.0)/365.25);
        }
        return 1;
    }
    return 0;
}
/* zenith tatol tropospheric delay (m),using unb3 model----------------------- */
extern double arc_tropmodel_unb3(gtime_t time, const double *pos, const double *azel,
                                double humi,double *zhd,double *zwd)
{
    const double g=9.80665,Rd=287.054,k1=77.604,k3_=382000.0;
    double hs,z,kd,args[10]={0.0},td2,tw2,gm,kw,zhd_=0.0,zwd_=0.0;

    if (pos[2]<-100.0||1E4<pos[2]||azel[1]<=0) return 0.0;

    hs=pos[2]<0.0?0.0:pos[2];
    if (zhd&&zwd) *zhd=*zwd=0.0;

    arc_get_amtf(pos,args,time);

    kd=pow(1.0-args[3]*hs/args[1],g/(Rd*args[3]));
    gm=9.782*(1.0-2.66E-3*cos(2.0*args[4])-2.8E-7*hs);
    td2=1E-6*k1*Rd/gm;
    zhd_=td2*kd*args[0];

    kw=pow(1.0-args[3]*hs/args[1],(1.0+args[4])*g/(Rd*args[3])-1.0);
    tw2=1E-6*k3_*Rd/(gm*(1.0+args[4])-args[3]*Rd);
    zwd_=tw2*kw*args[2]/args[1]; if (humi==0.0) zwd_=0.0;

    if (zhd) *zhd=zhd_; if (zwd) *zwd=zwd_;

    return zhd_+zwd_;
}
/* zenith tatol tropospheric delay (m),using unb3 model----------------------- */
extern double arc_tropmodel_mops(gtime_t time, const double *pos, const double *azel,
                                 double humi,double *zhd,double *zwd)
{
    const double g=9.80665,Rd=287.054,k1=77.604,k2=382000.0,gm=9.784;
    double hs,amt[10]={0.0},zhd_=0.0,zwd_=0.0;

    if (pos[2]<-100.0||1E4<pos[2]||azel[1]<=0) return 0.0;

    hs=pos[2]<0.0?0.0:pos[2];
    if (zhd&&zwd) *zhd=*zwd=0.0;

    arc_get_amtf(pos,amt,time);

    zhd_=1E-6*k1*amt[0]*Rd/gm*pow(1.0-amt[3]*hs/amt[1],g/(Rd*amt[3]));
    zwd_=1E-6*k2*Rd/(gm*(amt[4]+1.0)-amt[3]*Rd)*amt[2]/amt[1]*
         pow(1.0-amt[3]*hs/amt[1],(amt[4]+1.0)*g/(Rd*amt[3])-1.0);
    if (humi==0.0) zwd_=0.0;

    if (zhd) *zhd=zhd_; if (zwd) *zwd=zwd_;

    return zhd_+zwd_;
}
/*----------------------------------------------------------------------------*/
/* path tatol tropospheric delay (m),using GCAT model----------------------- */
extern double arc_tropmodel_gcat(gtime_t time, const double *pos, const double *azel,
                                 double humi)
{
    double hs,zhd,zwd=0.1,mf=1.0;

    if (pos[2]<-100.0||1E4<pos[2]||azel[1]<=0) return 0.0;

    hs=pos[2]<0.0?0.0:pos[2];

    mf=1.001/SQRT(0.002001+SQR(sin(azel[1])));

    zhd=2.3*exp(-0.116E-3*hs);
    if (humi==0.0) zwd=0.0;

    return (zhd+zwd)*mf;
}
/*----------------------------------------------------------------------------*/
extern double arc_tropmodel_black(gtime_t time, const double *pos, const double *azel,
                                  double humi,double *zhd,double *zwd)
{
    const double RE=RE_WGS84;
    double hs,zhd_=0.0,zwd_=0.0,hd,hw,lo,b,kd,kw;
    double T,P,e;

    if (pos[2]<-100.0||1E4<pos[2]||azel[1]<=0) return 0.0;

    hs=pos[2]<0.0?0.0:pos[2];

    /* atmosphere parameters */
    T=288.15-0.0068*hs;
    P=1013.25*pow(1.0-0.0068/288.15*hs,5);
    e=11.691*pow(1.0-0.0068/288.15*hs,4);

    hd=148.98*(T-3.96); hw=11000.0;
    lo=0.167+pow(0.076+0.15E-3*(T-273.16),-0.3*azel[1]);
    b=1.92/(SQR(azel[1]*R2D)+6);

    zhd_=0.002312*(T-3.96)*P/T; zwd_=0.00746542*e*hw/SQR(T);
    if (humi==0.0) zwd_=0.0;

    kd=SQRT(1.0-SQR(cos(azel[1])/(1.0+lo*hd/RE)))-b;
    kw=SQRT(1.0-SQR(cos(azel[1])/(1.0+lo*hw/RE)))-b;

    if (zhd) *zhd=zhd_; if (zwd) *zwd=zwd_;

    return zhd_/kd+zwd_/kw;
}
/*----------------------------------------------------------------------------*/
extern double arc_tropmodel_waas(gtime_t time, const double *pos, const double *azel,
                                 double humi)
{
    const double a0=1.264E-4,a15=1.509E-4,an=2.133E-4;
    double dh,dw,hs,Ns,t0v,t15v,lat,doy=time2doy(time);

    if (pos[2]<-100.0||1E4<pos[2]||azel[1]<=0) return 0.0;

    hs=pos[2]<0.0?0.0:pos[2]; lat=fabs(pos[0])*R2D;
    dh=pos[0]<0.0?335:152; dw=pos[0]<0.0?30:213;

    Ns=3.61E-3*hs*cos(2*PI*(doy-dh)/365)+lat*(-0.8225+0.1*cos(2*PI*(doy-dw)/365));
    t0v=2.506*(1.0+0.00125*Ns);
    t15v=2.484*(1.0+0.0015363*exp(-an*hs)*Ns);

    if (hs<=1500.0) {
        return t0v/sin(azel[1]+0.35*D2R)*(1.0-a0*hs);
    }
    else if (hs>1500.0) {
        return t15v/sin(azel[1]+0.35*D2R)*(1.0-a0*hs)*exp(1.0-a15*hs);
    }
    return 0.0;

}
/*----------------------------------------------------------------------------*/
extern double arc_tropmapf_cfa2_2(gtime_t time, const double *pos, const double *azel,
                                  double *mapfw)
{
    const double temp0=15.0; /* temparature at sea level */
    const double humi=0.7;
    double A,B,C,hgt,pres,temp,e,mh,mw,E=azel[1];

    if (pos[2]<-1000.0||pos[2]>20000.0) {
        if (mapfw) *mapfw=0.0;
        return 0.0;
    }
    /* standard atmosphere */
    hgt=pos[2]<0.0?0.0:pos[2];

    pres=1013.25*pow(1.0-2.2557E-5*hgt,5.2568);
    temp=temp0-6.5E-3*hgt+273.15;
    e=6.108*humi*exp((17.15*temp-4684.0)/(temp-38.45));

    A=0.00185*(1.0+6.071E-5*(pres-1000.0)-1.471E-4*e+3.072E-3*(temp-20.0));
    B=0.001144*(1.0+1.164E-5*(pres-1000.0)-2.795E-4*e+3.109E-3*(temp-20.0));
    C=-0.009;

    mw=mh=1.0/(sin(E)+A/(tan(E)+B/(sin(E)+C)));

    if (mapfw) *mapfw=mw;
    return mh;
}
/*----------------------------------------------------------------------------*/
extern double arc_tropmapf_chao(gtime_t time, const double *pos, const double *azel,
                                double *mapfw)
{
    double A,B,mh,mw;

    if (pos[2]<-1000.0||pos[2]>20000.0) {
        if (mapfw) *mapfw=0.0;
        return 0.0;
    }
    A=0.001433; B=0.0445;
    mh=1.0/(sin(azel[1])+A/(tan(azel[1])+B));

    A=0.00035; B=0.017;
    mw=1.0/(sin(azel[1])+A/(tan(azel[1])+B));

    if (mapfw) *mapfw=mw;
    return mh;
}
/*----------------------------------------------------------------------------*/
extern double arc_tropmapf_mtt(gtime_t time, const double *pos, const double *azel,
                               double *mapfw)
{
    const double temp0=15.0; /* temparature at sea level */
    const double humi=0.7;
    double A,B,C,hgt,pres,temp,e,mh,mw,E=azel[1];

    if (pos[2]<-1000.0||pos[2]>20000.0) {
        if (mapfw) *mapfw=0.0;
        return 0.0;
    }
    /* standard atmosphere */
    hgt=pos[2]<0.0?0.0:pos[2];

    pres=1013.25*pow(1.0-2.2557E-5*hgt,5.2568);
    temp=temp0-6.5E-3*hgt+273.15;
    e=6.108*humi*exp((17.15*temp-4684.0)/(temp-38.45));

    A=1.25003E-3*(1.0+6.258E-5*(pres-1000.0)+1.67337E-5*e+3.36152E-3*(temp-10.0));
    B=3.12108E-3*(1.0+3.8450E-5*(pres-1000.0)+6.62430E-5*e+4.62404E-3*(temp-10.0));
    C=6.945748E-2*(1.0+3.956E-5*(pres-1000.0)+2.94868E-3*(temp-10.0));
    mh=(1.0+A/(1.0+B/C))/(sin(E)+A/(sin(E)+B/(sin(E)+C)));

    A=5.741E-4; B=1.547E-3; C=4.88185E-2;
    mw=(1.0+A/(1.0+B/C))/(sin(E)+A/(sin(E)+B/(sin(E)+C)));

    if (mapfw) *mapfw=mw;
    return mh;
}
/*----------------------------------------------------------------------------*/
extern double arc_tropmapf_ma_mu(gtime_t time, const double *pos, const double *azel,
                                 double *mapfw)
{
    const double temp0=15.0; /* temparature at sea level */
    const double humi=0.7;
    double A,B,pres,temp,e,hs,beta,e0,f,ff,E=azel[1],mh,mw;

    if (pos[2]<-1000.0||pos[2]>20000.0) {
        if (mapfw) *mapfw=0.0;
        return 0.0;
    }
    /* standard atmosphere */
    hs=(pos[2]<0.0?0.0:pos[2])/1000.0;

    pres=1013.25*pow(1.0-2.2557E-5*hs,5.2568);
    temp=temp0-6.5E-3*hs+273.15;
    e=6.108*humi*exp((17.15*temp-4684.0)/(temp-38.45));

    e0=humi*6.11*pow(10.0,7.5*(temp-273.16)/temp);
    f=1.0-0.00266*cos(2.0*pos[1])-0.00028*hs;
    B=0.002277*(pres+(0.05+1255.0/temp)*e0)/f;
    ff=1.0-0.00266*cos(2.0*pos[1])-0.00031*hs;
    A=2.644E-3*exp(-0.14372*hs)/ff;

    beta=A/B;
    mw=mh=(1.0+beta)/(sin(E)+(beta/(1.0+beta)/(sin(E)+0.015)));
    if (mapfw) *mapfw=mw;
    return mh;
}
/*----------------------------------------------------------------------------*/
extern double arc_tropmapf_ifadis(gtime_t time, const double *pos, const double *azel,
                                  double *mapfw)
{
    const double temp0=15.0; /* temparature at sea level */
    const double humi=0.7;

    double a,b,c,pres,temp,e,hs,mh,mw,E=azel[1];

    if (pos[2]<-1000.0||pos[2]>20000.0) {
        if (mapfw) *mapfw=0.0;
        return 0.0;
    }
    /* standard atmosphere */
    hs=(pos[2]<0.0?0.0:pos[2]);

    pres=1013.25*pow(1.0-2.2557E-5*hs,5.2568);
    temp=temp0-6.5E-3*hs+273.15;
    e=6.108*humi*exp((17.15*temp-4684.0)/(temp-38.45));

    a=0.001237+0.1316E-6*(pres-1000.0)+0.1378E-5*(temp-15.0)+0.8057E-5*SQRT(e);
    b=0.003333+0.1946E-6*(pres-1000.0)+0.104E-5*(temp-15.0)+0.1747E-4*SQRT(e);
    c=0.078;
    mh=(1.0+a/(1.0+b/(1.0+c)))/(sin(E)+a/(sin(E)+b/(sin(E)+c)));

    a=0.0005236+0.2471E-6*(pres-1000.0)-0.1724E-6*(temp-15.0)+0.1328E-4*SQRT(e);
    b=0.001705+0.7384E-6*(pres-1000.0)+0.3767E-6*(temp-15.0)+0.2147E-4*SQRT(e);
    c=0.05917;
    mw=(1.0+a/(1.0+b/(1.0+c)))/(sin(E)+a/(sin(E)+b/(sin(E)+c)));

    if (mapfw) *mapfw=mw; return mh;
}
/*----------------------------------------------------------------------------*/
extern double arc_tropmapf_exp(gtime_t time, const double *pos, const double *azel,
                               double *mapfw)
{
    const double H=8452.4;
    const double C1=-1.265518,C2=1.000427,C3=0.3761631,C4=0.1870715,
            C5=-0.4751701,C6=0.9814044,C7=-1.864333,
            C8=1.194816,C9=-0.02725696,C10=-0.1368659;
    const double RE=RE_WGS84;
    double hs,E=azel[1],ro;
    double K,U,mh,mw,CU;

    if (pos[2]<-1000.0||pos[2]>20000.0) {
        if (mapfw) *mapfw=0.0;
        return 0.0;
    }
    /* standard atmosphere */
    hs=(pos[2]<0.0?0.0:pos[2]); ro=fabs(RE+hs);

    K=SQRT(ro/(2.0*H))*tan(E); U=1.0/(1.0+0.5*K);

    CU=C1*pow(U,0)+C2*pow(U,1)+C3*pow(U,2)+C4*pow(U,3)+C5*pow(U,4)+
       C6*pow(U,5)+C7*pow(U,6)+C8*pow(U,7)+C9*pow(U,8);
    mw=mh=SQRT(PI)/cos(E)*U*SQRT(ro/(2.0*H))*exp(CU);

    if (mapfw) *mapfw=mw; return mh;
}
/*----------------------------------------------------------------------------*/
extern double arc_tropmapf_UNSW931(gtime_t time, const double *pos, const double *azel,
                                   double *mapfw)
{
    const double temp0=15.0; /* temparature at sea level */
    const double H=8452.4,hT=11.132;
    const double humi=0.7,beta=3.5;
    double D1,D2,D3,D4,hs,z=PI/2.0-azel[1],I,mh,mw,T,P,e,r0=RE_WGS84;

    if (pos[2]<-1000.0||pos[2]>20000.0) {
        if (mapfw) *mapfw=0.0;
        return 0.0;
    }
    /* standard atmosphere */
    hs=(pos[2]<0.0?0.0:pos[2]); r0+=hs;
    T=temp0-6.5E-3*hs;
    P=1013.25*pow(1.0-2.2557E-5*hs,5.2568);
    e=6.108*humi*exp((17.15*T-4684.0)/(T+273.16-38.45));

    I=SQRT(2.0*r0/H)/tan(z);

    D1=0.4613893+2.864E-5*(P-1013.25)+8.99E-6*e-6.98E-6*e*e-1.0914E-4*(T-15.0)+
       1.30E-6*(T-15.0)*(T-15.0)+9.4694E-3*(beta+6.5)-2.4946E-3*(hT-11.231)+1.8072E-4*(hT-11.231)*(hT-11.231);
    D2=0.8276476+2.056E-5*(P-1013.25)+2.3820E-4*e-4.76E-6*e*e+5.1125E-4*(T-15.0)+
       1.23E-6*(T-15.0)*(T-15.0)+3.6479E-2*(beta+6.5)-1.5321E-2*(hT-11.231)+9.4802E-4*(hT-11.231)*(hT-11.231);
    D3=2.531492+1.093E-4*(P-1013.25)+2.6179E-3*e+1.33E-5*e*e+3.7103E-3*(T-15.0)+
       4.95E-6*(T-15.0)*(T-15.0)+1.6022E-1*(beta+6.5)-8.998E-2*(hT-11.231)+4.9496E-3*(hT-11.231)*(hT-11.231);
    D4=47.07844+1.595E-3*(P-1013.25)+3.9026E-2*e+2.41E-4*e*e-4.1713E-2*(T-15.0)+
       2.16E-4*(T-15.0)*(T-15.0)+1.6313*(beta+6.5)-9.9757E-1*(hT-11.231)+4.4528E-2*(hT-11.231)*(hT-11.231);
    mh=1.0/(cos(z)+D1/(I*I/cos(z)+D2/(cos(z)+D3/(I*I/cos(z)+D4))));

    D1=0.4815575+2.7294274E-5*(P-1013.25)+2.4902E-10*(P-1013.25)*(P-1013.25)-2.1572975E-3*(T-15.0)+(T-15.0)*(T-15.0)-
       1.1699250E-2*(beta+6.5)+9.1246967E-4*(e-1.0)+7.1638049E-3*(hT-11.231);
    D2=0.812254+2.9278208E-5*(P-1013.25)+7.9563810E-9*(P-1013.25)*(P-1013.25)-2.7441928E-3*(T-15.0)-
       3.3017573E-3*(beta+6.5)+4.5324398E-4*(e-1.0)+2.1880062E-6*(e-1.0)*(e-1.0)+8.9074254E-3*(hT-11.231)+
       6.075414E-14*(hT-11.231)*(hT-11.231);
    D3=2.342219-8.8508130E-5*(P-1013.25)+4.3200293E-8*(P-1013.25)*(P-1013.25)-7.8798437E-3*(T-15.0)-
       1.9310775E-2*(beta+6.5)+1.7527907E-3*(e-1.0)+1.2091468E-5*(e-1.0)*(e-1.0)+9.3512393E-4*(hT-11.231)+
       3.248198E-3*(hT-11.231)*(hT-11.231);
    D4=44.8141601-1.2605952E-3*(P-1013.25)+5.4042844E-7*(P-1013.25)*(P-1013.25)-0.16031622*(T-15.0)-
       0.25365998*(beta+6.5)+2.3692612E-2*(e-1.0)+1.663844E-4*(e-1.0)*(e-1.0)-0.12135797*(hT-11.231)+
       3.6598847E-2*(hT-11.231)*(hT-11.231);
    mw=1.0/(cos(z)+D1/(I*I/cos(z)+D2/(cos(z)+D3/(I*I/cos(z)+D4))));

    if (mapfw) *mapfw=mw;
    return mh;
}
/* complementaty error function (ref [1] p.227-229) --------------------------*/
static double arc_q_gamma(double a, double x, double log_gamma_a);
static double arc_p_gamma(double a, double x, double log_gamma_a)
{
    double y,w;
    int i;

    if (x==0.0) return 0.0;
    if (x>=a+1.0) return 1.0-arc_q_gamma(a,x,log_gamma_a);

    y=w=exp(a*log(x)-x-log_gamma_a)/a;

    for (i=1;i<100;i++) {
        w*=x/(a+i);
        y+=w;
        if (fabs(w)<1E-15) break;
    }
    return y;
}
static double arc_q_gamma(double a, double x, double log_gamma_a)
{
    double y,w,la=1.0,lb=x+1.0-a,lc;
    int i;

    if (x<a+1.0) return 1.0-arc_p_gamma(a,x,log_gamma_a);
    w=exp(-x+a*log(x)-log_gamma_a);
    y=w/lb;
    for (i=2;i<100;i++) {
        lc=((i-1-a)*(lb-la)+(i+x)*lb)/i;
        la=lb; lb=lc;
        w*=(i-1-a)/i;
        y+=w/la/lb;
        if (fabs(w/la/lb)<1E-15) break;
    }
    return y;
}
static double f_erfc(double x)
{
    return x>=0.0?arc_q_gamma(0.5,x*x,LOG_PI/2.0):1.0+arc_p_gamma(0.5,x*x,LOG_PI/2.0);
}
/* confidence function of integer ambiguity ----------------------------------*/
extern double arc_conffunc(int N, double B, double sig)
{
    double x,p=1.0;
    int i;

    x=fabs(B-N);
    for (i=1;i<8;i++) {
        p-=f_erfc((i-x)/(SQRT2*sig))-f_erfc((i+x)/(SQRT2*sig));
    }
    return p;
}
/* search bds sat no in its list------------------------------------------------*/
static int arc_search_sat_geo(const char* prn)
{
    int i;
    for (i=0;i<NUMOFGEO;i++) {
        if (bds_geo[i]=="") break; /* todo:this condition may be have bugs */
        if (strcmp(prn,bds_geo[i])==0) return 1;
    }
    return 0;
}
/* excluded bds geo satellites--------------------------------------------------*/
extern void arc_exclude_bds_geo(prcopt_t *opt)
{
    int i;
    char prn[6];

    for (i=0;i<MAXSAT;i++) {
        if (satsys(i+1,NULL)!=SYS_CMP) continue;
        satno2id(i+1,prn);  /* todo:maybe have more bette way to excluded bds geo satellites */
        if (arc_search_sat_geo(prn)) opt->exsats[i]=1;  /* set excluded flag */
    }
    arc_log(ARC_INFO,"arc_exclude_bds_geo : excluded bds geo list = %4s %4s %4s %4s",
            bds_geo[0],bds_geo[1],bds_geo[2],bds_geo[3]);
}
/* judge this satellites whether it is bds geo---------------------------------*/
extern int arc_is_bds_geo(int sat)
{
    char prn[8]; satno2id(sat,prn); return arc_search_sat_geo(prn);
}
/* chi-square distribution---------------------------------------------------- */
extern double arc_chi2(int n,double x,double *f)
{
    double iai;
    double p,Ux;
    double pi=3.14159265358979312;

    double y=x/2.0;
    if(n%2) {
        Ux=sqrt(y/pi)*exp(-y);
        p=2.0*arc_norm_distri(sqrt(x))-1.0;
        iai=0.5;
    }
    else {
        Ux=y*exp(-y);
        p=1.0-exp(-y);
        iai=1.0;
    }
    while(iai!=0.5*n)  {
        p=p-Ux/iai;
        Ux=Ux*y/iai;
        iai+=1.0;
    }
    *f=Ux/x; return p;
}
/* normal distribution functional -------------------------------------------*/
extern double arc_norm_distri(const double u)
{
    if(u<-5.0) return 0.0;
    if(u>5.0) return 1.0;

    double y=fabs(u)/sqrt(2.0);

    double p=1.0+y*(0.0705230784+y*(0.0422820123+y*(0.0092705272+
             y*(0.0001520143+y*(0.0002765672+y*0.0000430638)))));
    double er =1- pow( p, -16.0 );
    p=(u<0.0)? 0.5-0.5*er: 0.5+0.5*er;
    return p;
}
extern double arc_re_norm(double p)
{
    if(p==0.5) return 0.0;
    if(p>0.9999997) return 5.0;
    if(p<0.0000003) return -5.0;
    if(p<0.5) return -arc_re_norm(1.0-p);

    double y=-log(4.0*p*(1.0-p));
    y=y*(1.570796288+y*(0.3706987906e-1
      +y*(-0.8364353589e-3+y*(-0.2250947176e-3
      +y*(0.6841218299e-5+y*(0.5824238515e-5
      +y*(-0.1045274970e-5+y*(0.8360937017e-7
      +y*(-0.3231081277e-8+y*(0.3657763036e-10
      +y*0.6936233982e-12))))))))));
    return sqrt(y);
}
extern double arc_re_chi2(int n,double p)
{
    if(p>0.9999999)p=0.9999999;
    if(n==1)  {
        double x=arc_re_norm((1.0-p)/2.0);
        return x*x;
    }
    if(n==2) return -2.0*log(1.0-p);

    double u=arc_re_norm(p);
    double w=2.0/(9.0*n);
    double x0=1.0-w+u*sqrt(w);
    x0=n*x0*x0*x0;

    while(1) {
        double f;
        double pp=arc_chi2(n,x0,&f);
        if(f+1.0==1.0)return x0;
        double xx=x0-(pp-p)/f;
        if(fabs(x0-xx)<0.001) return xx;
        x0=xx;
    }
}
static void hhbg(int n,double *a)
{
    int i,j,k,u,v;
    double d,t;
    for (k=1;k<=n-2;k++) {
        d=0.0;
        for (j=k;j<=n-1;j++){
            u=j*n+k-1; t=a[u];
            if (fabs(t)>fabs(d)) { d=t; i=j;}
        }
        if (fabs(d)+1.0!=1.0) {
            if (i!=k) {
                for (j=k-1;j<=n-1;j++) {
                    u=i*n+j; v=k*n+j;
                    t=a[u]; a[u]=a[v]; a[v]=t;
                }
                for (j=0;j<=n-1;j++) {
                    u=j*n+i; v=j*n+k;
                    t=a[u]; a[u]=a[v]; a[v]=t;
                }
            }
            for (i=k+1;i<=n-1;i++) {
                u=i*n+k-1; t=a[u]/d; a[u]=0.0;
                for (j=k; j<=n-1; j++) {
                    v=i*n+j;
                    a[v]=a[v]-t*a[k*n+j];
                }
                for (j=0;j<=n-1;j++) {
                    v=j*n+k; a[v]=a[v]+t*a[j*n+i];
                }
            }
        }
    }
    return;
}
static int qrtt(int n,double *a,double *u,double *v,double eps,int jt)
{
    int m,it,i,j,k,l,ii,jj,kk,ll;
    double b,c,w,g,xy,p,q,r,x,s,e,f,z,y;
    it=0; m=n;
    while (m!=0) {
        l=m-1;
        while ((l>0)&&(fabs(a[l*n+l-1])
                       >eps*(fabs(a[(l-1)*n+l-1])+fabs(a[l*n+l])))) l=l-1;
        ii=(m-1)*n+m-1; jj=(m-1)*n+m-2;
        kk=(m-2)*n+m-1; ll=(m-2)*n+m-2;
        if (l==m-1) {
            u[m-1]=a[(m-1)*n+m-1]; v[m-1]=0.0;
            m=m-1; it=0;
        }
        else if (l==m-2) {
            b=-(a[ii]+a[ll]);
            c=a[ii]*a[ll]-a[jj]*a[kk];
            w=b*b-4.0*c;
            y=sqrt(fabs(w));
            if (w>0.0) {
                xy=1.0;
                if (b<0.0) xy=-1.0;
                u[m-1]=(-b-xy*y)/2.0;
                u[m-2]=c/u[m-1];
                v[m-1]=0.0; v[m-2]=0.0;
            }
            else {
                u[m-1]=-b/2.0; u[m-2]= u[m-1];
                v[m-1]= y/2.0;  v[m-2]=-v[m-1];
            }
            m=m-2; it=0;
        }
        else {
            if (it>=jt)  return -1;
            it=it+1;
            for (j=l+2;j<=m-1;j++) a[j*n+j-2]=0.0;
            for (j=l+3;j<=m-1;j++) a[j*n+j-3]=0.0;
            for (k=l;k<=m-2;k++) {
                if (k!=l) {
                    p=a[k*n+k-1]; q=a[(k+1)*n+k-1]; r=0.0;
                    if (k!=m-2) r=a[(k+2)*n+k-1];
                }
                else { x=a[ii]+a[ll];
                    y=a[ll]*a[ii]-a[kk]*a[jj];
                    ii=l*n+l; jj=l*n+l+1;
                    kk=(l+1)*n+l; ll=(l+1)*n+l+1;
                    p=a[ii]*(a[ii]-x)+a[jj]*a[kk]+y;
                    q=a[kk]*(a[ii]+a[ll]-x);
                    r=a[kk]*a[(l+2)*n+l+1];
                }
                if ((fabs(p)+fabs(q)+fabs(r))!=0.0) {
                    xy=1.0;
                    if (p<0.0) xy=-1.0;
                    s=xy*sqrt(p*p+q*q+r*r);
                    if (k!=l) a[k*n+k-1]=-s;
                    e=-q/s; f=-r/s; x=-p/s;
                    y=-x-f*r/(p+s);
                    g=e*r/(p+s);
                    z=-x-e*q/(p+s);
                    for (j=k;j<=m-1;j++) {
                        ii=k*n+j; jj=(k+1)*n+j; p=x*a[ii]+e*a[jj];
                        q=e*a[ii]+y*a[jj]; r=f*a[ii]+g*a[jj];
                        if (k!=m-2) {
                            kk=(k+2)*n+j; p=p+f*a[kk];
                            q=q+g*a[kk]; r=r+z*a[kk]; a[kk]=r;
                        }
                        a[jj]=q; a[ii]=p;
                    }
                    j=k+3;
                    if (j>=m-1) j=m-1;
                    for (i=l; i<=j; i++) {
                        ii=i*n+k; jj=i*n+k+1;
                        p=x*a[ii]+e*a[jj]; q=e*a[ii]+y*a[jj]; r=f*a[ii]+g*a[jj];
                        if (k!=m-2) {
                            kk=i*n+k+2; p=p+f*a[kk];
                            q=q+g*a[kk];r=r+z*a[kk]; a[kk]=r;
                        }
                        a[jj]=q; a[ii]=p;
                    }
                }
            }
        }
    }
    return 1;
}
/* eigenvalue of matrix ----------------------------------------------------------*/
extern int arc_mateigenvalue(const double* A,int n,double *u,double *v)
{
    int i=0;
    double *_A_=arc_mat(n,n);

    arc_matcpy(_A_,A,n,n);
    hhbg(n,_A_);

    i=qrtt(n,_A_,u,v,EPS,ITERS); if (i<=0) return 0;

    free(_A_); return 1;
}
extern int arc_matdet(const double*A,int n,double*det)
{
    int i;
    double* u=arc_mat(n,1),*v=arc_mat(n,1),p=1.0,q=0.0,tmp;

    if (!arc_mateigenvalue(A,n,u,v)) return 0;

    for (i=0;i<n;i++) {
        tmp=p; p=u[i]*p-v[i]*q; q=v[i]*tmp+u[i]*q;
    }
    if (det) *det=p;

    free(u);free(v);
    return 1;
}
extern double arc_normcdf(double X)
{
    double A1=0.4361836;
    double A2=-0.1201676;
    double A3=0.9372980;
    double P=0.33267;
    double Eps=1.0E-12;
    double T;

    if (X>0.0) T=X; else T=-X; T=(1.0+P*T);

    if ((T>-Eps)&&(T<Eps)) T=Eps;

    T=1.0/T;
    T=1.0-exp(-0.5*X*X)*(T*(A1+T*(A2+A3*T)))/sqrt(2.0*PI);

    return ((X>0.0)?T:1.0-T);
}
/* dummy functions for lex extentions ----------------------------------------*/
#ifndef EXTLEX
#endif /* EXTLEX */

