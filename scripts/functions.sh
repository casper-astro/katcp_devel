export CORR_CONFIG=/etc/corr/config
export CORR_INIT_SCRIPT=/etc/init.d/corr
export KATCP_SERVER=localhost:1235
export CORR_MAPPING=/var/run/corr/antenna_mapping

kcs_check_timeout()
{
  if [ $(date +%s) -gt $1 ] ; then
    echo "#log error $(date +%s)000 script request\_to\_corr\_timed\_out"
    return 1
  fi
}

kcs_input_to_index () {
  echo $[$(echo $1 | tr -d -c [:digit:])*2+$(echo $1 | tr -dc xy | tr xy 01)]
}

kcs_index_to_input () {
  echo $[i/2]$(echo $[i%2] | tr 01 xy)
}

kcs_debug () {
  echo "#log debug $(date +%s)000 script $(echo $1 | sed -e 's/ /\\_/g')"
}

kcs_warn () {
  echo "#log warn $(date +%s)000 script $(echo $1 | sed -e 's/ /\\_/g')"
}

kcs_error () {
  echo "#log error $(date +%s)000 script $(echo $1 | sed -e 's/ /\\_/g')"
  return 1
}

kcs_info () {
  echo "#log info $(date +%s)000 script $(echo $1 | sed -e 's/ /\\_/g')"
}

kcs_change_corr()
{
  kcs_debug "checking for running corr"

  if ps ax | grep -q corr[_-]katcp[_-]interface ; then
    kcs_info "stopping corr"
    ${CORR_INIT_SCRIPT} stop
  fi

  if [ -n "$1" ] ; then

    kcs_debug "attempting to set config file"

    if [ -e ${CORR_CONFIG}-${1} ] ; then
      if [ -h ${CORR_CONFIG} ] ; then
        kcs_debug "unlinking old configuration"
        rm -f ${CORR_CONFIG}
      else 
        kcs_error "refusing to clobber configuration ${CORR_CONFIG} - expected a symlink"
        return 1
      fi
      kcs_debug "updating configuration to ${CORR_CONFIG}-${1}"
      ln -s ${CORR_CONFIG}-${1} ${CORR_CONFIG}
    else 
      kcs_error "no $1 configuration for corr at $${CORR_CONFIG}-${1}"
      return 1
    fi

  fi

  kcs_debug "restarting corr"
  ${CORR_INIT_SCRIPT} start

  sleep 1
}

kcs_arg_check () {
  if [ $1 -lt $2 ] ; then
    kcs_error "insufficient parameters"
    return 1
  fi
}

kcs_corr_log () {
  kcs_debug "retrieving correlator logs"
  if kcpcmd -k -m get-log ; then
    kcpcmd -k -r clr-log
  fi
  kcs_debug "finished getting correlator logs"
  return 0
}

kcs_config_numeric () {
  value=$(grep ^${1} ${CORR_CONFIG} 2> /dev/null | cut -f2 -d= | tr -d ' ' )
  if [ -z "${value}" ] ; then
    kcs_error "unable to locate ${1} in ${CORR_CONFIG}"
  fi

  kcs_debug "${1} maps to ${value}"

  export $1=${value}
}

kcs_mode_sensors () {
  if [ -n "${1}" ] ;  then
    matching_mode=${1}
  else
    kcs_error "mode sensors needs to be invoked with sensor parameter"
  fi

  sensor_suffixes=("number\_of\_channels none integer 0 65536" "number\_of\_chanels none integer 0 65536" "current\_selected\_center\_frequency Hz integer 0 1000000000" "number\_of\_channels none integer 0 65536")
  sensor_names=(".nbc.channels.coarse" ".nbc.channels.fine" ".nbc.frequency.current" ".wbc.channels")
  sensor_keys=(coarse_chans n_chans "" n_chans)
  sensor_values=("" "" "0" "")
  sensor_stata=("" "" "unknown" "")

  i=0
  while [ $i -lt ${#sensor_names[*]} ] ; do 

    sensor_name=${sensor_names[$i]}
    sensor_key=${sensor_keys[$i]}
    sensor_suffix=${sensor_suffixes[$i]}
    sensor_value=${sensor_values[$i]}

    if [ "${sensor_name:0:1}" = "." ] ; then
      relative=${sensor_name:1}
      mode=${relative%%.*}
    else 
      mode=${sensor_name%%.*}
    fi

    config=${CORR_CONFIG}-${mode}

    kcs_debug "attempting to locate $sensor_key in ${config} (mode ${mode}) for ${sensor_name}"

    if [ "${mode}" = "${matching_mode}" ] ; then
      sensor_status="nominal"
    else
      sensor_status="unknown"
    fi

    temp=${sensor_stata[$i]}
    if [ -n "${temp}" ] ; then
      sensor_status=${temp}
    fi

    # try to look up a value in file, clobbering what is set statically above
    if [ -e ${config} ] ; then
      if [ -n "${sensor_key}" ] ; then
        temp=$(grep ^${sensor_key} ${config} 2> /dev/null | cut -f2 -d= | tr -d ' ' )
        if [ -n "${temp}" ] ; then
          sensor_value=${temp}
        fi
      fi
    fi

    # if we have a value, print the sensor 
    if [ -n "${sensor_value}" ] ; then
      echo "#sensor-list ${sensor_name} ${sensor_suffix}"
      echo "#sensor-status $(date +%s)000 1 ${sensor_name} ${sensor_status} ${sensor_value}"
    fi

    i=$[i+1]
  done
}

