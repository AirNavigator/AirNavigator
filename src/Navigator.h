//============================================================================
// Name        : Navigator.h
// Since       : 19/2/2011
// Author      : Alberto Realis-Luc <alberto.realisluc@gmail.com>
// Web         : http://www.alus.it/airnavigator/
// Copyright   : (C) 2010-2013 Alberto Realis-Luc
// License     : GNU GPL v2
// Repository  : https://github.com/AirNavigator/AirNavigator.git
// Last change : 17/11/2013
// Description : Header of the navigation manager: Navigator.c
//============================================================================

#ifndef NAVIGATOR_H_
#define NAVIGATOR_H_

#define NAV_STATUS_NOT_INIT         0
#define NAV_STATUS_NO_ROUTE_SET     1
#define NAV_STATUS_TO_START_NAV     2
#define NAV_STATUS_WAIT_FIX         3
#define NAV_STATUS_NAV_TO_DPT       4
#define NAV_STATUS_NAV_TO_WPT       5
#define NAV_STATUS_NAV_TO_DST       6
#define NAV_STATUS_NAV_TO_SINGLE_WP 7
#define NAV_STATUS_END_NAV          8
#define NAV_STATUS_NAV_BUSY         9

typedef struct wp {
	int seqNo;            //sequence number
	char *name;           //name of the WP
	double latitude;      //rad
	double longitude;     //rad
	double altitude;      //meters
	double dist;          //distance to to this WP in rad
	double initialCourse; //initial true course to to this WP in rad
	double finalCourse;   //final true course to this WP in rad
	double bisector1;     //bisector in rad between this leg and the next
	double bisector2;     //opposite bisector to bisector1 in rad
	double arrTimestamp;  //arrival to this WP timestamp in seconds (from h 0:00)
	struct wp *prev;      //Pointer to the previous waypoint in the list
	struct wp *next;      //Pointer to the next waypoint in the list
} *wayPoint;

int NavLoadFlightPlan(char* GPXfile);
void NavAddWayPoint(double latWP, double lonWP, double altWP, char *WPname);
void NavClearRoute(void);
void NavUpdatePosition(double lat, double lon, double alt, double speed, double dir, float timestamp);
short checkDaytime(short calcOnlyDest);
void NavStartNavigation(float timestamp);
int NavReverseRoute(void);
void NavSkipCurrentWayPoint(void);
float getCurrentTime(void);
void NavClose(void);

#endif /*NAVIGATOR_H_*/
