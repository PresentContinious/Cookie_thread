/* Single source of truth for CoAP resource paths. */

#ifndef COOKIE_PROTO_RESOURCES_H_
#define COOKIE_PROTO_RESOURCES_H_

#define COOKIE_RESOURCE_SENSORS_PATH    "sensors/data"
#define COOKIE_RESOURCE_SENSORS_SEG_0   "sensors"
#define COOKIE_RESOURCE_SENSORS_SEG_1   "data"

/* Static path-segments array, NULL-terminated, suitable for
 * coap_resource.path on the server side. */
#define COOKIE_RESOURCE_SENSORS_SEGS \
	(const char * const []){ COOKIE_RESOURCE_SENSORS_SEG_0, \
				 COOKIE_RESOURCE_SENSORS_SEG_1, \
				 NULL }

#endif /* COOKIE_PROTO_RESOURCES_H_ */
