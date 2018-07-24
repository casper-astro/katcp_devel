await_mode_sensors() {
  echo "#sensor-status $(date +%s)000 1 .channels unknown 0"
  echo "#sensor-status $(date +%s)000 1 .centerfrequency unknown 0"
  echo "#sensor-status $(date +%s)000 1 .bandwidth unknown 0"

  for name in bf0 bf1 ; do
    echo "#sensor-status $(date +%s)000 1 .${name}.bandwidth unknown 0"
    echo "#sensor-status $(date +%s)000 1 .${name}.centerfrequency unknown 0"
  done
}

fetch_config_sensors() {
  
  config_file="$1"
  status="$2"
#  prefix="$3"

  if [ ! -f "${config_file}" ] ; then
    kcpmsg -s mode -l warn "no configuration in ${config_file}"
    return 0
  fi

  if [ -z "${status}" ] ; then
    kcpmsg -s mode -l warn "bad status field ${status}"
    return 0
  fi

#   if [ -n "${prefix}" ] ; then
#     if [ "${prefix:0:1}" != '.' ] ; then
#       kcpmsg -s mode -l warn "mode prefix ${prefix} probably malformed"
#     fi
#   fi

  channels=$(grep ^n_chans ${config_file} 2> /dev/null | cut -f2 -d= | tr -d ' ' )
  if [ -n "${channels}" ] ; then

    adc_clock=$(grep ^adc_clk ${config_file} 2> /dev/null | cut -f2 -d= | tr -d ' ' )
    coarse_channels=$(grep ^coarse_chans ${config_file} 2> /dev/null | cut -f2 -d= | tr -d ' ' )

    bandwidth=$(echo "${adc_clock:-800}/(${coarse_channels:-1}*2)" | bc -l | cut -f1 -d. )
    if [ -n "${coarse_channels}" ] ; then
      centerfrequency=0
    else
      centerfrequency=$(echo "${bandwidth}/2" | bc -l | cut -f1 -d.)
    fi 

    echo "#sensor-status $(date +%s)000 1 .channels ${status} ${channels}"
    echo "#sensor-status $(date +%s)000 1 .centerfrequency ${status} ${centerfrequency}"
    echo "#sensor-status $(date +%s)000 1 .bandwidth ${status} ${bandwidth}"

  fi

}

setup_static_sensors () {
  echo "#sensor-list .channels number\_of\_channels none integer 0 65536"

  echo "#sensor-list .centerfrequency current\_center\_frequency Hz integer 0 500000000"
  echo "#sensor-list .bandwidth bandwidth\_of\_current\_mode Hz integer 0 1000000000"

  for name in bf0 bf1 ; do 
    echo "#sensor-list .${name}.centerfrequency selected\_center\_frequency\_of\_beam Hz integer 0 500000000"
    echo "#sensor-list .${name}.bandwidth selected\_bandwidth\_of\_beam Hz integer 0 1000000000"
  done
}

change_mode_sensors () {
  from="$1"
  to="$2"

  if [ -z "${from}" -o -z "${to}" ] ; then
    kcpmsg -s mode -l warn "no static sensors to update"
    exit 0
  fi

#   if [ -f ${CORR_CONFIG}/${from} ]  ; then
#     echo "#sensor-status $(date +%s)000 1 .${from}.centerfrequency unknown \@"
#     echo "#sensor-status $(date +%s)000 1 .${from}.channels unknown \@"
#     echo "#sensor-status $(date +%s)000 1 .${from}.bandwidth unknown \@"
#   fi
# 
#   if [ -f ${CORR_CONFIG}/${to} ] ; then
#     echo "#sensor-status $(date +%s)000 1 .${to}.centerfrequency nominal \@"
#     echo "#sensor-status $(date +%s)000 1 .${to}.channels nominal \@"
#     echo "#sensor-status $(date +%s)000 1 .${to}.bandwidth nominal \@"
#   fi

  fetch_config_sensors ${CORR_CONFIG}/${to} nominal
}


# change_mode_sensors () {
#   if [ -n "${2}" ] ;  then
#     matching_mode=${2}
#   else
#     kcs_error "mode sensors needs to be invoked with sensor parameter"
#   fi
# 
#   sensor_suffixes=("number\_of\_channels none integer 0 65536" "number\_of\_chanels none integer 0 65536" "current\_selected\_center\_frequency Hz integer 0 1000000000" "number\_of\_channels none integer 0 65536" "number\_of\_channels none integer 0 65536" "number\_of\_channels none integer 0 65536")
#   sensor_names=(".nbc.channels.coarse" ".nbc.channels" ".nbc.centerfrequency" ".wbc.channels" ".wbc8k.channels" ".bwbc4a.channels")
#   sensor_keys=(coarse_chans n_chans "" n_chans n_chans n_chans)
#   sensor_values=("" "" "0" "" "" "")
#   sensor_stata=("" "" "unknown" "" "" "")
# 
#   i=0
#   while [ $i -lt ${#sensor_names[*]} ] ; do 
# 
#     sensor_name=${sensor_names[$i]}
#     sensor_key=${sensor_keys[$i]}
#     sensor_suffix=${sensor_suffixes[$i]}
#     sensor_value=${sensor_values[$i]}
# 
#     if [ "${sensor_name:0:1}" = "." ] ; then
#       relative=${sensor_name:1}
#       mode=${relative%%.*}
#     else 
#       mode=${sensor_name%%.*}
#     fi
# 
#     config=${CORR_CONFIG}/${mode}
# 
#     kcs_debug "attempting to locate $sensor_key in ${config} (mode ${mode}) for ${sensor_name}"
# 
#     if [ "${mode}" = "${matching_mode}" ] ; then
#       sensor_status="nominal"
#     else
#       sensor_status="unknown"
#     fi
# 
#     temp=${sensor_stata[$i]}
#     if [ -n "${temp}" ] ; then
#       sensor_status=${temp}
#     fi
# 
#     # try to look up a value in file, clobbering what is set statically above
#     if [ -e ${config} ] ; then
#       if [ -n "${sensor_key}" ] ; then
#         temp=$(grep ^${sensor_key} ${config} 2> /dev/null | cut -f2 -d= | tr -d ' ' )
#         if [ -n "${temp}" ] ; then
#           sensor_value=${temp}
#         fi
#       fi
#     fi
# 
#     # if we have a value, print the sensor 
#     if [ -n "${sensor_value}" ] ; then
#       echo "#sensor-list ${sensor_name} ${sensor_suffix}"
#       echo "#sensor-status $(date +%s)000 1 ${sensor_name} ${sensor_status} ${sensor_value}"
#     fi
# 
#     i=$[i+1]
#   done
# }
