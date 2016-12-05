/*
 * picoTU.c
 *
 *  Created on: 25 mars 2015
 *      Author: nico
 */

#include <stdlib.h>
#include <stdio.h>

#include "picows.h"


/*TEST*/
int WS_Hello( void * _service, void * _request)
{
	WEBSERVICE * service     = _service;
	HTTPD_REQUEST * request  = _request;
	printf("Run service %s\n", service->name);
	printf("Request %s\n", request->url);

	sprintf( service->page, "Hello World");

	return 1;
}


int WS_Hello_JSON( void * _service, void * _request)
{
	WEBSERVICE * service     = _service;
	HTTPD_REQUEST * request  = _request;
	printf("Run service %s\n", service->name);
	printf("Request %s\n", request->url);

	char * template = "{"
            "   \"name\": \"%s\","
            "   \"value\": \"%s\""
            "}";
	sprintf( service->page, template, "HelloWorldJSON", "Hello World !!!");

	return 1;
}


int WS_Hello_XML( void * _service, void * _request)
{
	WEBSERVICE * service     = _service;
	HTTPD_REQUEST * request  = _request;
	printf("Run service %s\n", service->name);
	printf("Request %s\n", request->url);

	char * template = "<?xml version=\"1.0\"?>"
            "<hello>"
            "   <name>%s</name>"
            "   <value>%s</value>"
            "</hello>";
	sprintf( service->page, template, "HelloWorldXML", "Hello World !!!");

	return 1;
}


/*MAIN*/
int main(int argc, char * argv[])
{
	unsigned short port = 9999;
	char * path = ".";

	if (argc >= 2) 			port = atoi( argv[1] );
	if (argc >= 3) 			path = argv[2];

	char Html_Buffer[1024];
	WEBSERVICE webService[] = {
			{
				.service = WS_Hello,
				.name    = "Hello",
				.page    = Html_Buffer,
			},
			{
				.service = WS_Hello_JSON,
				.name    = "HelloJSON",
				.page    = Html_Buffer,
			},
			{
				.service = WS_Hello_XML,
				.name    = "HelloXML",
				.page    = Html_Buffer,
			}
	};

	picows_load( sizeof(webService)/sizeof(WEBSERVICE), webService);
	picows_start( port, path);

	return 0;
}
