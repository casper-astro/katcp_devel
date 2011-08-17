/* setup routines, simple, but doesn't check for existing sensors */

struct katcp_acquire *a;

snprintf(name, ... "%s.lur", roach);

sn = find_sensor_katcp(d, name);
if(sn){ /* sensor already exists */
  a = acquire_from_sensor_katcp(d, sn);
  if(a == NULL){
    /* logic failure, marc messed up */
  }

} else { /* sensor needs to be created */

  a = setup_boolean_acquire_katcp(d, NULL, NULL, NULL);

  if(a == NULL){
    return -1;
  }

  register_direct_multi_boolean_sensor_katcp(d, 0, name, "roach is functional" , "none", a);
#if 0
  register_direct_multi_boolean_sensor_katcp(d, 0, "roachxxx.lru", "roach is functional" , "none", a);
#endif

}

/* now squirrel away *a, so that it can be updated later */

free(name);



/* updating the sensor value */

set_boolean_acquire_katcp(d, a, 1);

set_boolean_acquire_katcp(d, a, 0);
