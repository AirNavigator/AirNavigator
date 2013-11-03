//============================================================================
// Name        : Navigator.c
// Since       : 19/2/2011
// Author      : Alberto Realis-Luc <alberto.realisluc@gmail.com>
// Web         : http://www.alus.it/airnavigator/
// Copyright   : (C) 2010-2013 Alberto Realis-Luc
// License     : GNU GPL v2
// Repository  : https://github.com/AirNavigator/AirNavigator.git
// Last change : 2/11/2013
// Description : Navigation manager
//============================================================================

//FIXME: detected NaN for course and XTD for some rare case
//TODO: manage better the navigation to the departure

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <libroxml/roxml.h>
#include "Navigator.h"
#include "Configuration.h"
#include "AirCalc.h"
#include "NMEAreader.h"
#include "FbRender.h"
#include "HSI.h"
#include "AirNavigator.h"


int status=STATUS_NOT_INIT, numWayPoints=0;
double trueCourse, previousAltitude=-1000;
double totalDistKm, prevWPsTotDist; //Km
double prevWpAvgSpeed, prevTotAvgSpeed; //Km/h
wayPoint dept, currWP, dest;
char *routeLogPath;
FILE *routeLog=NULL;
double sunZenith; //here in rad

void NavConfigure() {
	int oldStatus=status;
	status=STATUS_NAV_BUSY;
	sunZenith=Deg2Rad(config.sunZenith); //rad
	prevWpAvgSpeed=config.cruiseSpeed;
	prevTotAvgSpeed=config.cruiseSpeed;
	if(oldStatus==STATUS_NOT_INIT) status=STATUS_NO_ROUTE_SET;
	else status=oldStatus;
}

void NavCalculateRoute() {
	if(numWayPoints<2 || dept==NULL || dept->next==NULL) return;
	if(status==STATUS_NO_ROUTE_SET) {
		status=STATUS_NAV_BUSY;
		PrintNavStatus(status,"Unknown");
	} else if(status!=STATUS_NAV_BUSY) return;
	totalDistKm=0;
	dest=currWP;
	currWP=dept->next;
	do {
		currWP->trueCourse=calcGreatCircleRoute(currWP->prev->latitude,currWP->prev->longitude,currWP->latitude,currWP->longitude,&currWP->dist);
		double remainDist=Rad2Km(currWP->dist);
		totalDistKm+=remainDist;
		fprintf(routeLog,"* Travel from %s to %s\n",currWP->prev->name,currWP->name);
		fprintf(routeLog,"Initial course to WP: %06.2f°\n",Rad2Deg(currWP->trueCourse));
		fprintf(routeLog,"Horizontal distance to WP is: %.3f Km\n",remainDist);
		double timeHours=remainDist/config.cruiseSpeed; //hours
		if(currWP==dept->next) PrintNavRemainingDistWP(remainDist,config.cruiseSpeed,timeHours);
		int hours,mins;
		float secs;
		convertDecimal2DegMinSec(timeHours,&hours,&mins,&secs);
		fprintf(routeLog,"Flight time to WP: %2d:%02d:%02d\n",hours,mins,(int)secs);
		fprintf(routeLog,"Initial altitude: %.0f m  %.0f Ft\n",currWP->prev->altitude,m2Ft(currWP->prev->altitude));
		fprintf(routeLog,"Final altitude: %.0f m  %.0f Ft\n",currWP->altitude,m2Ft(currWP->altitude));
		double altDiff=currWP->altitude-currWP->prev->altitude;
		double slope=altDiff/1000/remainDist;
		fprintf(routeLog,"Slope: %.2f %%\n",slope*100);
		//double lineDist=sqrt(pow(remainDist,2)+pow(altDiff/1000,2));
		double climb=altDiff/(timeHours*3600); // m/s
		fprintf(routeLog,"Estimated climb rate: %.2f m/s  %.0f Ft/min\n\n",climb,ms2FtMin(climb)); //just to have an idea
		currWP=currWP->next;
	} while(currWP!=NULL);
	fprintf(routeLog,"TOTAL distance from departure along all WPs to destination is: %.3f Km\n",totalDistKm);
	double totalTimeHours=totalDistKm/config.cruiseSpeed;
	int hours,mins;
	float secs;
	convertDecimal2DegMinSec(totalTimeHours,&hours,&mins,&secs);
	fprintf(routeLog,"TOTAL flight time: %2d:%02d:%02d\n",hours,mins,(int)secs);
	float time=gps.timestamp;
	if(time<0) time=getCurrentTime(); //In this case we don't have the time from GPS so we take it from the internal clock
	time=time/3600+totalTimeHours; //hours, in order to obtain the ETA
	PrintNavRemainingDistDST(totalDistKm,config.cruiseSpeed,time);
	double fuelNeeded=config.fuelConsumption*totalTimeHours;
	fprintf(routeLog,"TOTAL fuel needed: %.2f liters\n\n",fuelNeeded);
	checkDaytime();
	if(routeLog!=NULL) fclose(routeLog); //Close the route log file when not needed
	prevWPsTotDist=0;
	status=STATUS_TO_START_NAV;
	PrintNavStatus(status,dept->next->name);
	FbRender_Flush();
}

int NavLoadFlightPlan(char* GPXfile) {
	if(status==STATUS_NOT_INIT) NavConfigure();
	if(status==STATUS_NOT_INIT) return 0;
	NavClearRoute();
	routeLogPath=strdup(GPXfile);
	int len=strlen(routeLogPath);
	routeLogPath[len-3]='t';
	routeLogPath[len-2]='x';
	routeLogPath[len-1]='t';
	routeLog=fopen(routeLogPath,"w"); //Create the route log file: NavCalculateRoute() will write in it
	if(routeLog==NULL) {
		logText("ERROR not possible to write the route log file.\n");
		status=STATUS_NO_ROUTE_SET;
		return 0;
	}
	node_t* root=roxml_load_doc(GPXfile);
	if(root==NULL) {
		logText("ERROR no such file '%s'\n",GPXfile);
		return 0;
	}
	char* text=NULL;
	//TODO: here we load just the first route may be there are others routes in the GPX file...
	node_t* route=roxml_get_chld(root,"rte",0);
	if(route==NULL) {
		logText("ERROR no route found in GPX file: '%s'\n",GPXfile);
		roxml_release(RELEASE_ALL);
		roxml_close(root);
		return 0;
	}
	node_t* wp;
	int total=roxml_get_chld_nb(route);
	if(total<2) {
		logText("ERROR no enough waypoints found in route in GPX file: '%s'\n",GPXfile);
		roxml_release(RELEASE_ALL);
		roxml_close(root);
		return 0;
	}
	int wpcounter=0,i;
	node_t* node;
	double latX,lonX,altX;
	for(i=0;i<total;i++) {
		wp=roxml_get_chld(route,NULL,i);
		text=roxml_get_name(wp,NULL,0);
		if(strcmp(text,"rtept")==0) {
			wpcounter++;
			node=roxml_get_attr(wp,"lat",0);
			if(node!=NULL) {
				text=roxml_get_content(node,NULL,0,NULL);
				latX=atof(text);
			} else break; //WP without latitude, skip it
			node=roxml_get_attr(wp,"lon",0);
			if(node!=NULL) {
				text=roxml_get_content(node,NULL,0,NULL);
				lonX=atof(text);
			} else break; //WP without longitude, skip it
			node=roxml_get_chld(wp,"ele",0);
			if(node!=NULL) {
				text=roxml_get_content(node,NULL,0,NULL);
				if(text!=NULL) altX=atof(text);
				else altX=0;
			} else altX=0; //WP altitude missing we put it to 0
			node=roxml_get_chld(wp,"name",0);
			if(node!=NULL) {
				text=roxml_get_content(node,NULL,0,NULL);
				if(text==NULL) asprintf(&text,"Unamed WP %d",wpcounter);
			} else asprintf(&text,"Unamed WP %d",wpcounter);
			NavAddWayPoint(Deg2Rad(latX),Deg2Rad(-lonX),altX,text);
		}
	}
	roxml_release(RELEASE_ALL);
	roxml_close(root);
	logText("Loaded route with %d WayPoints.\n\n",wpcounter);
	NavCalculateRoute();
	return 1;
}

void NavAddWayPoint(double latWP, double lonWP, double altWPmt, char *WPname) {
	if(status!=STATUS_NO_ROUTE_SET) return;
	wayPoint newWP;
	newWP=(wayPoint)malloc(sizeof(struct wp));
	newWP->name=strdup(WPname);
	newWP->latitude=latWP;
	newWP->longitude=lonWP;
	newWP->altitude=altWPmt;
	newWP->next=NULL;
	if(numWayPoints==0) { //its's the first
		dept=newWP;
		newWP->prev=NULL;
	} else {
		currWP->next=newWP;
		newWP->prev=currWP; //link to the previous
	}
	currWP=newWP;
	newWP->seqNo=numWayPoints++;
}

int NavReverseRoute() {
	if(status==STATUS_NAV_BUSY || status==STATUS_NO_ROUTE_SET) return 0;
	status=STATUS_NAV_BUSY;
	PrintNavStatus(status,"Reversing...");
	dest=dept;
	currWP=dept;
	wayPoint next;
	int i=numWayPoints-1;
	while(currWP->next!=NULL) {
		currWP->seqNo=i--;
		next=currWP->next;
		currWP->next=currWP->prev;
		currWP->prev=next;
		currWP=next;
	}
	currWP->seqNo=0;
	currWP->next=currWP->prev;
	currWP->prev=NULL;
	dept=currWP;
	currWP=dest;
	routeLog=fopen(routeLogPath,"a+"); //Reopen the route log file to wrire about the reversed route
	if(routeLog==NULL) {
		logText("ERROR not possible to write the route log file.\n");
		status=STATUS_NO_ROUTE_SET;
		return 0;
	}
	fprintf(routeLog,"\n\n\nREVERSED ROUTE\n\n");
	NavCalculateRoute();
	return 1;
}

void NavClearRoute() {
	status=STATUS_NAV_BUSY;
	PrintNavStatus(status,"Unknown");
	if(numWayPoints!=0) {
		do {
			currWP=dept;
			free(currWP->name);
			dept=currWP->next;
			free(currWP);
		} while(dept!=NULL);
		numWayPoints=0;
	}
	free(routeLogPath);
	PrintNavStatus(status,"Nowhere");
	status=STATUS_NO_ROUTE_SET;
}

void NavClose() {
	NavClearRoute();
	status=STATUS_NOT_INIT;
}

void NavFindNextWP(double lat, double lon) {
	if(status!=STATUS_TO_START_NAV && status!=STATUS_WAIT_FIX) return;
	wayPoint i, nearest=dept;
	double shortestDist=calcAngularDist(lat,lon,dept->latitude,dept->longitude);
	for(i=dept->next;i!=NULL;i=i->next) { //searching the nearest
		double distance=calcAngularDist(lat,lon,i->latitude,i->longitude);
		if(distance<=shortestDist) {
			shortestDist=distance;
			nearest=i;
		}
	}
	logText("\nNearest waypoint is: %s No: %d at %f Km\n",nearest->name,nearest->seqNo,Rad2Km(shortestDist));
	if(nearest==dest) {
		if(dept->latitude==dest->latitude && dept->longitude==dest->longitude) { //dept and dest are the same place
			if(calcAngularDist(lat,lon,dept->latitude,dept->longitude)<m2Rad(config.deptDistTolerance)) currWP=dept->next;
			else currWP=dest;
		} else currWP=dest;
	} else { //The nearest WP isn't the destination
		if(calcAngularDist(lat,lon,nearest->next->latitude,nearest->next->longitude)<=nearest->next->dist) currWP=nearest->next;
		else currWP=nearest;
	}
	if(currWP==dept) {
		if(calcAngularDist(lat,lon,dept->latitude,dept->longitude)<m2Rad(config.deptDistTolerance)) { //we are near the dpt
			currWP=dept->next;
			status=STATUS_NAV_TO_WPT;
		} else status=STATUS_NAV_TO_DPT; //we have still to go to the departure
	} else { //we are not traveling to the departure
		if(currWP->seqNo>1) { //  //currWP->prev!=dept  if we aren't traveling direcly from the departure
			prevWPsTotDist=0;
			for(i=dept->next;i!=currWP;i=i->next) prevWPsTotDist+=i->dist;
			prevWPsTotDist=Rad2Km(prevWPsTotDist);
			double time=(prevWPsTotDist/config.cruiseSpeed)*3600; //time to reach the previous WP
			currWP->prev->arrTimestamp=dept->arrTimestamp+time; //ETA to the previous WP
		}
		status=STATUS_NAV_TO_WPT;
	}

	if(currWP==dest) status=STATUS_NAV_TO_DST;
	PrintNavStatus(status,currWP->name);
	logText("Next waypoint is: %s\n\n\n",currWP->name);
	FbRender_Flush();
}

void NavStartNavigation(float timestamp) { //timestamp have to be the real time when we start the travel
	if(status!=STATUS_TO_START_NAV || timestamp<0) return;
	short fixMode=gps.fixMode;
	if(fixMode>MODE_NO_FIX) { //if we have a fix we give immediately the position to the nav...
		double lat=gps.lat;
		if(lat!=100) { //just to avoid the case when we have a fix but still not a position stored
			double lon=gps.lon;
			NavFindNextWP(lat,lon);
			if(status==STATUS_NAV_TO_WPT || status==STATUS_NAV_TO_DST) dept->arrTimestamp=timestamp; //here we record the starting time for whole route
			NavUpdatePosition(lat,lon,gps.realAltMt,gps.speedKmh,gps.trueTrack,timestamp);
		} else {
			currWP=dept->next;
			status=STATUS_WAIT_FIX;
		}
	} else {
		currWP=dept->next;
		status=STATUS_WAIT_FIX;
	}
	PrintNavStatus(status,currWP->name);
	FbRender_Flush();
}

void updateDtgEteEtaAs(double atd, float timestamp, double remainDist) {
	double averageSpeed; //Km/h
	double time; //Hours
	if(timestamp>currWP->prev->arrTimestamp) { //to avoid infinite, null or negative speed and time
		remainDist=Rad2Km(remainDist); //Km
		if(atd>=0) {
			averageSpeed=ms2Kmh(Rad2m(atd)/(timestamp-currWP->prev->arrTimestamp));
			prevWpAvgSpeed=averageSpeed;
		} else averageSpeed=prevWpAvgSpeed; //with negative ATDs we estimate using previous average speed
		time=remainDist/averageSpeed; //ETE (remaining time) in hours
		PrintNavRemainingDistWP(remainDist,averageSpeed,time);
	}
	if(timestamp>dept->arrTimestamp) {
		double totCoveredDistKm=prevWPsTotDist+Rad2Km(atd);
		if(atd>=0) {
			averageSpeed=ms2Kmh((totCoveredDistKm*1000)/(timestamp-dept->arrTimestamp)); //Km/h
			prevTotAvgSpeed=averageSpeed;
		} else averageSpeed=prevTotAvgSpeed;
		remainDist=totalDistKm-totCoveredDistKm; //Km
		time=remainDist/averageSpeed; //hours
		time+=timestamp/3600; //hours, in order to obtain the ETA
		PrintNavRemainingDistDST(remainDist,averageSpeed,time);
	}
}

void NavUpdatePosition(double lat, double lon, double altMt, double speedKmh, double dir, float timestamp) {
	double actualTrueCourse, remainDist;
	switch(status) {
		case STATUS_NOT_INIT: //Do nothing
			break;
		case STATUS_TO_START_NAV:
			if(previousAltitude==-1000) previousAltitude=altMt;
			else if(altMt-previousAltitude>config.takeOffdiffAlt && speedKmh>config.stallSpeed) {
				NavStartNavigation(timestamp);
				break;
			}
			actualTrueCourse=calcGreatCircleRoute(lat,lon,dept->next->latitude,dept->next->longitude,&remainDist); //calc just course and distance
			PrintNavDTG(remainDist);
			HSIupdateCDI(Rad2Deg(actualTrueCourse),0);
			break;
		case STATUS_NAV_TO_DPT: //we are still going to the departure point
			actualTrueCourse=calcGreatCircleRoute(lat,lon,dept->latitude,dept->longitude,&remainDist); //calc just course and distance
			PrintNavDTG(remainDist);
			HSIupdateCDI(Rad2Deg(actualTrueCourse),0);
			if(remainDist<m2Rad(config.deptDistTolerance)) {
				currWP=dept->next;
				dept->arrTimestamp=timestamp; //here we record the starting time for whole route
				if(currWP!=dest) status=STATUS_NAV_TO_WPT;
				else status=STATUS_NAV_TO_DST; //Next WP is already the final destination
				PrintNavStatus(status,currWP->name);
				NavUpdatePosition(lat,lon,altMt,speedKmh,dir,timestamp); //recursive call
			}
			break;
		case STATUS_NAV_TO_WPT: {
			double atd, trackErr;
			trackErr=Rad2m(calcGCCrossTrackError(currWP->prev->latitude,currWP->prev->longitude,currWP->longitude,lat,lon,currWP->trueCourse,&atd));
			if(atd>=0) remainDist=currWP->dist-atd;
			else remainDist=currWP->dist+fabs(atd); //negative ATD: we are still before the prev WP
			actualTrueCourse=calcGreatCircleCourse(lat,lon,currWP->latitude,currWP->longitude); //Find the direct direction to curr WP (needed to check if we passed the bisector)
			if(atd>=0 && (bisectorOverpassed(currWP->trueCourse,currWP->next->trueCourse,actualTrueCourse) || atd>=currWP->dist)) { //consider this WP as reached
				currWP->arrTimestamp=timestamp;

				//////Bisector TESTING:
				int hour, min,latDeg,lonDeg,latMin,lonMin;
				float sec,latSec,lonSec;
				convertTimestamp2HourMinSec(timestamp,&hour,&min,&sec);
				convertRad2DegMinSec(lat,&latDeg,&latMin,&latSec);
				convertRad2DegMinSec(lon,&lonDeg,&lonMin,&lonSec);
				logText("*** %d:%d:%f Bisector crossed lat: %d %d %f - lon: %d %d %f \n",hour,min,sec,latDeg,latMin,latSec,lonDeg,lonMin,lonSec);
				//////END of TESTING

				prevWPsTotDist+=Rad2Km(currWP->dist);
				currWP=currWP->next;
				if(currWP==dest) status=STATUS_NAV_TO_DST; //Next WP is the final destination
				PrintNavStatus(status,currWP->name);
				NavUpdatePosition(lat,lon,altMt,speedKmh,dir,timestamp); //Recursive call on the new WayPoint
				return;
			} //else the WP or bisector is still not reached...
			if(fabs(trackErr)>config.trackErrorTolearnce) { //if we have bigger error
				double latI,lonI; //the perpendicular point on the route
				calcIntermediatePoint(currWP->prev->latitude,currWP->prev->longitude,currWP->latitude,currWP->longitude,atd,currWP->dist,&latI,&lonI);
				actualTrueCourse=calcGreatCircleCourse(latI,lonI,currWP->latitude,currWP->longitude);
			} //else with a small error the actualTrueCourse calculated before is fine
			HSIupdateCDI(Rad2Deg(actualTrueCourse),trackErr);
			PrintNavTrackATD(atd);
			if(atd>=0) HSIupdateVSI(m2Ft((currWP->altitude-currWP->prev->altitude)/currWP->dist*atd+currWP->prev->altitude));
			updateDtgEteEtaAs(atd,timestamp,remainDist);
		} break;
		case STATUS_NAV_TO_DST: {
			double atd, trackErr;
			trackErr=Rad2m(calcGCCrossTrackError(currWP->prev->latitude,currWP->prev->longitude,currWP->longitude,lat,lon,currWP->trueCourse,&atd));
			if(atd>=0) remainDist=currWP->dist-atd;
			else remainDist=currWP->dist+fabs(atd); //negative ATD: we are still before the prev WP
			if(atd>=currWP->dist) { //consider destination as reached (90 degrees bisector)
				currWP->arrTimestamp=timestamp;
				status=STATUS_END_NAV;
				PrintNavStatus(status,"Nowhere");
				NavUpdatePosition(lat,lon,altMt,speedKmh,dir,timestamp); //Recursive call on the new WayPoint
				return;
			} //else the destination is still not reached...
			if(fabs(trackErr)<config.trackErrorTolearnce) //with a really small error
				actualTrueCourse=calcGreatCircleCourse(lat,lon,currWP->latitude,currWP->longitude); //just find the direct direction to the destination
			else { //otherwise we have bigger error and so we calculate it better...
				double latI,lonI; //the perpendicular point on the route
				calcIntermediatePoint(currWP->prev->latitude,currWP->prev->longitude,currWP->latitude,currWP->longitude,atd,currWP->dist,&latI,&lonI);
				actualTrueCourse=calcGreatCircleCourse(latI,lonI,currWP->latitude,currWP->longitude);
			}
			PrintNavTrackATD(atd);
			HSIupdateCDI(Rad2Deg(actualTrueCourse),trackErr);
			if(atd>=0) HSIupdateVSI(m2Ft((currWP->altitude-currWP->prev->altitude)/currWP->dist*atd+currWP->prev->altitude));
			updateDtgEteEtaAs(atd,timestamp,remainDist);
		} break;
		case STATUS_END_NAV: { //We have reached or passed the destination
			actualTrueCourse=calcGreatCircleRoute(lat,lon,dest->latitude,dest->longitude,&remainDist); //calc just course and distance
			PrintNavDTG(remainDist);
			HSIupdateCDI(Rad2Deg(actualTrueCourse),0);
		} break;
		case STATUS_NAV_BUSY: //Do nothing
			break;
		case STATUS_WAIT_FIX:
			NavFindNextWP(lat,lon);
			NavUpdatePosition(lat,lon,altMt,speedKmh,dir,timestamp);
			break;
	}
}

void NavSkipCurrentWayPoint() {
	if(status==STATUS_NAV_TO_WPT || status==STATUS_NAV_TO_DST) {
		status=STATUS_NAV_BUSY;
		PrintNavStatus(status,"Skipping next WP");
		if(currWP==dest) {
			status=STATUS_END_NAV;
			PrintNavStatus(status,"Nowhere");
			return;
		}
		double lat=gps.lat;
		double lon=gps.lon;
		float timestamp=gps.timestamp;
		currWP->arrTimestamp=timestamp; //we put the arrival timestamp when we skip it
		double atd;
		calcGCCrossTrackError(currWP->prev->latitude,currWP->prev->longitude,currWP->longitude,lat,lon,currWP->trueCourse,&atd);
		prevWPsTotDist+=Rad2Km(atd); //we add only the ATD
		totalDistKm-=Rad2Km(currWP->dist)-Rad2Km(atd); //from the total substract the skipped reamaining dst
		totalDistKm-=currWP->next->dist; //from the total substract the dst from skipped wp to the next
		currWP=currWP->next; //jump to the next
		currWP->trueCourse=calcGreatCircleRoute(lat,lon,currWP->latitude,currWP->longitude,&currWP->dist); //recalc course
		totalDistKm+=Rad2Km(currWP->dist); //we add the new dst form current pos to the next WP
		if(currWP!=dest) status=STATUS_NAV_TO_WPT;
		else status=STATUS_NAV_TO_DST;
		PrintNavStatus(status,currWP->name);
	}
}

short checkDaytime() {
	short retVal=1;
	double riseTime,setTime;
	struct tm time_str;
	long timestamp=time(NULL); //get current time
	time_str=*localtime(&timestamp); //to obtain day, month, year, hour and minute
	int year=time_str.tm_year+1900; //year
	int month=time_str.tm_mon+1; //month
	int day=time_str.tm_mday; //day
	int hour=time_str.tm_hour; //hour
	int min=time_str.tm_min; //minute
	fprintf(routeLog,"Check daytime on: %d/%02d/%d %2d:%02d\n",day,month,year,hour,min);
	calcSunriseSunset(dept->latitude,dept->longitude,day,month,year,sunZenith,+1,&riseTime,&setTime);
	if(riseTime==-1){
		fprintf(routeLog,"WARNING: The sun never rises on the departure location (on the specified date).\n");
		retVal=0;
	}
	double depTimeDecimal=hour+min*SIXTYTH;
	if(depTimeDecimal<riseTime || depTimeDecimal>setTime) {
		fprintf(routeLog,"WARNING: Departure at this time will be in the night.\n");
		retVal=0;
	}
	int rHour,rMin,sHour,sMin;
	float rSec,sSec;
	convertDecimal2DegMinSec(riseTime,&rHour,&rMin,&rSec);
	convertDecimal2DegMinSec(setTime,&sHour,&sMin,&sSec);
	fprintf(routeLog,"Departure location Sunrise: %2d:%02d, Sunset: %2d:%02d \n",rHour,rMin,sHour,sMin);
	double arrTimeDecimal=depTimeDecimal+totalDistKm/config.cruiseSpeed;
	if(arrTimeDecimal>24) {
		timestamp+=arrTimeDecimal*3600;
		time_str=*localtime(&timestamp); //to obtain day, month, year, hour and minute
		year=time_str.tm_year+1900; //year
		month=time_str.tm_mon+1; //month
		day=time_str.tm_mday; //day
	}
	calcSunriseSunset(dest->latitude,dest->longitude,day,month,year,sunZenith,+1,&riseTime,&setTime);
	if(riseTime==-1){
		fprintf(routeLog,"WARNING: The sun never rises on the arrival location (on the specified date).\n");
		retVal=0;
	}
	if(arrTimeDecimal<riseTime || arrTimeDecimal>setTime) {
		fprintf(routeLog,"WARNING: Arrival at this time will be in the night.\n");
		retVal=0;
	}
	convertDecimal2DegMinSec(riseTime,&rHour,&rMin,&rSec);
	convertDecimal2DegMinSec(setTime,&sHour,&sMin,&sSec);
	fprintf(routeLog,"Arrival location Sunrise: %2d:%02d, Sunset: %2d:%02d \n\n\n",rHour,rMin,sHour,sMin);
	return retVal;
}

float getCurrentTime() {
	struct tm time_str;
	long currTime=time(NULL); //get current time
	time_str=*localtime(&currTime);
	int n=time_str.tm_hour; //hour
	float time=n*3600;
	n=time_str.tm_min; //minute
	time+=n*60;
	n=time_str.tm_sec;
	time+=n;
	return time;
}
