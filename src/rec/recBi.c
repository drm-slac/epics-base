/* recBi.c */
/* share/src/rec  $Id$ */
  
/* recBi.c - Record Support Routines for Binary Input records */
/*
 *      Original Author: Bob Dalesio
 *      Current Author:  Marty Kraimer
 *      Date:            7-14-89
 *
 *	Experimental Physics and Industrial Control System (EPICS)
 *
 *	Copyright 1991, the Regents of the University of California,
 *	and the University of Chicago Board of Governors.
 *
 *	This software was produced under  U.S. Government contracts:
 *	(W-7405-ENG-36) at the Los Alamos National Laboratory,
 *	and (W-31-109-ENG-38) at Argonne National Laboratory.
 *
 *	Initial development by:
 *		The Controls and Automation Group (AT-8)
 *		Ground Test Accelerator
 *		Accelerator Technology Division
 *		Los Alamos National Laboratory
 *
 *	Co-developed with
 *		The Controls and Computing Group
 *		Accelerator Systems Division
 *		Advanced Photon Source
 *		Argonne National Laboratory
 *
 *
 * Modification Log:
 * -----------------
 * .01  12-12-88        lrd     lock the record on entry and unlock on exit
 * .02  12-15-88        lrd     Process the forward scan link
 * .03  12-23-88        lrd     Alarm on locked MAX_LOCKED times
 * .04  01-13-89        lrd     delete db_read_bi
 * .05  03-17-89        lrd     database link inputs
 * .06  03-29-89        lrd     make hardware errors MAJOR
 *                              remove hw severity spec from database
 * .07  04-06-89        lrd     add monitor detection
 * .08  05-03-89        lrd     removed process mask from arg list
 * .09  01-31-90        lrd     add the plc_flag arg to the ab_bidriver call
 * .10  03-21-90        lrd     add db_post_events for RVAL
 * .11  04-11-90        lrd     make local variables static
 * .12  10-10-90	mrk	Made changes for new record and device support
 * .13  10-24-91	jba	Moved comment
 * .14  11-11-91        jba     Moved set and reset of alarm stat and sevr to macros
 * .15  02-05-92	jba	Changed function arguments from paddr to precord 
 * .16  02-28-92	jba	ANSI C changes
 * .17  04-10-92        jba     pact now used to test for asyn processing, not status
 * .18  04-18-92        jba     removed process from dev init_record parms
 */

#include	<vxWorks.h>
#include	<types.h>
#include	<stdioLib.h>
#include	<lstLib.h>
#include	<string.h>

#include	<alarm.h>
#include	<dbDefs.h>
#include	<dbAccess.h>
#include	<dbFldTypes.h>
#include	<devSup.h>
#include	<errMdef.h>
#include	<recSup.h>
#include	<special.h>
#include	<biRecord.h>
/* Create RSET - Record Support Entry Table*/
#define report NULL
#define initialize NULL
static long init_record();
static long process();
#define special NULL
static long get_value();
#define cvt_dbaddr NULL
#define get_array_info NULL
#define put_array_info NULL
#define get_units NULL
#define get_precision NULL
static long get_enum_str();
static long get_enum_strs();
static long put_enum_str();
#define get_graphic_double NULL
#define get_control_double NULL
#define get_alarm_double NULL
struct rset biRSET={
	RSETNUMBER,
	report,
	initialize,
	init_record,
	process,
	special,
	get_value,
	cvt_dbaddr,
	get_array_info,
	put_array_info,
	get_units,
	get_precision,
	get_enum_str,
	get_enum_strs,
	put_enum_str,
	get_graphic_double,
	get_control_double,
	get_alarm_double };
struct bidset { /* binary input dset */
	long		number;
	DEVSUPFUN	dev_report;
	DEVSUPFUN	init;
	DEVSUPFUN	init_record; /*returns: (-1,0)=>(failure,success)*/
	DEVSUPFUN	get_ioint_info;
	DEVSUPFUN	read_bi;/*(0,2)=> success and convert, don't convert)*/
                        /* if convert then raw value stored in rval */
};
void alarm();
void monitor();

static long init_record(pbi,pass)
    struct biRecord	*pbi;
    int pass;
{
    struct bidset *pdset;
    long status;

    if (pass==0) return(0);

    if(!(pdset = (struct bidset *)(pbi->dset))) {
	recGblRecordError(S_dev_noDSET,pbi,"bi: init_record");
	return(S_dev_noDSET);
    }
    /* must have read_bi function defined */
    if( (pdset->number < 5) || (pdset->read_bi == NULL) ) {
	recGblRecordError(S_dev_missingSup,pbi,"bi: init_record");
	return(S_dev_missingSup);
    }
    if( pdset->init_record ) {
	if((status=(*pdset->init_record)(pbi))) return(status);
    }
    return(0);
}

static long process(pbi)
	struct biRecord     *pbi;
{
	struct bidset	*pdset = (struct bidset *)(pbi->dset);
	long		 status;
	unsigned char    pact=pbi->pact;

	if( (pdset==NULL) || (pdset->read_bi==NULL) ) {
		pbi->pact=TRUE;
		recGblRecordError(S_dev_missingSup,pbi,"read_bi");
		return(S_dev_missingSup);
	}

	status=(*pdset->read_bi)(pbi); /* read the new value */
	/* check if device support set pact */
	if ( !pact && pbi->pact ) return(0);
	pbi->pact = TRUE;

	tsLocalTime(&pbi->time);
	if(status==0) { /* convert rval to val */
		if(pbi->rval==0) pbi->val =0;
		else pbi->val = 1;
		pbi->udf = FALSE;
	}
	else if(status==2) status=0;
	/* check for alarms */
	alarm(pbi);
	/* check event list */
	monitor(pbi);
	/* process the forward scan link record */
	if (pbi->flnk.type==DB_LINK) dbScanPassive(((struct dbAddr *)pbi->flnk.value.db_link.pdbAddr)->precord);

	pbi->pact=FALSE;
	return(status);
}

static long get_value(pbi,pvdes)
    struct biRecord		*pbi;
    struct valueDes	*pvdes;
{
    pvdes->field_type = DBF_ENUM;
    pvdes->no_elements=1;
    (unsigned short *)(pvdes->pvalue) = &pbi->val;
    return(0);
}

static long get_enum_str(paddr,pstring)
    struct dbAddr *paddr;
    char	  *pstring;
{
    struct biRecord	*pbi=(struct biRecord *)paddr->precord;

    if(pbi->val==0) {
	strncpy(pstring,pbi->znam,sizeof(pbi->znam));
	pstring[sizeof(pbi->znam)] = 0;
    } else if(pbi->val==1) {
	strncpy(pstring,pbi->onam,sizeof(pbi->onam));
	pstring[sizeof(pbi->onam)] = 0;
    } else {
	strcpy(pstring,"Illegal_Value");
    }
    return(0);
}

static long get_enum_strs(paddr,pes)
    struct dbAddr *paddr;
    struct dbr_enumStrs *pes;
{
    struct biRecord	*pbi=(struct biRecord *)paddr->precord;

    pes->no_str = 2;
    memset(pes->strs,'\0',sizeof(pes->strs));
    strncpy(pes->strs[0],pbi->znam,sizeof(pbi->znam));
    strncpy(pes->strs[1],pbi->onam,sizeof(pbi->onam));
    return(0);
}
static long put_enum_str(paddr,pstring)
    struct dbAddr *paddr;
    char          *pstring;
{
    struct biRecord     *pbi=(struct biRecord *)paddr->precord;

    if(strncmp(pstring,pbi->znam,sizeof(pbi->znam))==0) pbi->val = 0;
    else  if(strncmp(pstring,pbi->onam,sizeof(pbi->onam))==0) pbi->val = 1;
    else return(S_db_badChoice);
    return(0);
}


static void alarm(pbi)
    struct biRecord	*pbi;
{
	unsigned short val = pbi->val;


        if(pbi->udf == TRUE){
                recGblSetSevr(pbi,UDF_ALARM,VALID_ALARM);
                return;
        }

	if(val>1)return;
        /* check for  state alarm */
        if (val == 0){
                recGblSetSevr(pbi,STATE_ALARM,pbi->zsv);
        }else{
                recGblSetSevr(pbi,STATE_ALARM,pbi->osv);
        }

        /* check for cos alarm */
	if(val == pbi->lalm) return;
        recGblSetSevr(pbi,COS_ALARM,pbi->cosv);
	pbi->lalm = val;
	return;
}

static void monitor(pbi)
    struct biRecord	*pbi;
{
	unsigned short	monitor_mask;
        short           stat,sevr,nsta,nsev;

        /* get previous stat and sevr  and new stat and sevr*/
        recGblResetSevr(pbi,stat,sevr,nsta,nsev);

	monitor_mask = 0;

	/* alarm condition changed this scan */
	if (stat!=nsta || sevr!=nsev){
		/* post events for alarm condition change*/
		monitor_mask = DBE_ALARM;
		/* post stat and sevr fields */
		db_post_events(pbi,&pbi->stat,DBE_VALUE);
		db_post_events(pbi,&pbi->sevr,DBE_VALUE);
	}
        /* check for value change */
        if (pbi->mlst != pbi->val){
                /* post events for value change and archive change */
                monitor_mask |= (DBE_VALUE | DBE_LOG);
                /* update last value monitored */
                pbi->mlst = pbi->val;
        }

	/* send out monitors connected to the value field */
	if (monitor_mask){
		db_post_events(pbi,&pbi->val,monitor_mask);
	}
	if(pbi->oraw!=pbi->rval) {
		db_post_events(pbi,&pbi->rval,monitor_mask|DBE_VALUE);
		pbi->oraw = pbi->rval;
	}
	return;
}
