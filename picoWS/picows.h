/*
 * picows.h
 *
 *  Created on: 2 avr. 2014
 *      Author: nico
 */

#ifndef PICO_WS_H_
#define PICO_WS_H_


/*
 *
 * REQUEST definition
 *
 */
typedef enum {

	HTTPD_REQUEST_UNIMPLEMENTED = 0,
	HTTPD_REQUEST_FILE,
	HTTPD_REQUEST_CGI,
	HTTPD_REQUEST_WEBSERVICE,

	HTTPD_REQUEST_CLOSE,


	HTTPD_REQUEST_KIND_NB,

} HTTPD_REQUEST_KIND;


typedef struct {
	int                       client;
	HTTPD_REQUEST_KIND        kind;
	char                      method[256];
	char                      path  [512];
	char                      query [512];
	char                      url   [1024];

} HTTPD_REQUEST;


/*
 *
 * WEB service definition
 *
 */
typedef int (*WEBSERVICE_HANDLER)(void *,void*);

typedef struct {
	WEBSERVICE_HANDLER service;   //Fonction
	const char *       name;      //Name of the webserver
	char *             page;      //Result page
} WEBSERVICE;

extern int picows_load( int nb, WEBSERVICE * aWebService );

extern int picows_start( unsigned short port, const char * root_path);


#endif /* PICO_WS_H_ */
