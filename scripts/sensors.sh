
setup_static_sensors () {
  for config_file in ${CORR_CONFIG}/* ; do
    if [ -f ${config_file} ] ; then
      channels=$(grep ^n_chans ${config_file} 2> /dev/null | cut -f2 -d= | tr -d ' ' )
      mode=${config_file##*/}
      if [ -n "${channels}" ] ; then
        echo "#sensor-list .${mode}.channels number\_of\_channels none integer 0 65536"
        echo "#sensor-list .${mode}.centerfrequency current\_center\_frequency Hz integer 0 500000000"
        echo "#sensor-list .${mode}.bandwidth bandwidth\_of\_current\_mode Hz integer 0 1000000000"

        adc_clock=$(grep ^adc_clk ${config_file} 2> /dev/null | cut -f2 -d= | tr -d ' ' )
        coarse_channels=$(grep ^coarse_chans ${config_file} 2> /dev/null | cut -f2 -d= | tr -d ' ' )

        bandwidth=$(echo "${adc_clock:-800}/(${coarse_channels:-1}*2)" | bc -l | cut -f1 -d. )
        centerfrequency=$(echo "${bandwidth}/2" | bc -l | cut -f1 -d.)

        echo "#sensor-status $(date +%s)000 1 .${mode}.channels unknown ${channels}"
        echo "#sensor-status $(date +%s)000 1 .${mode}.centerfrequency unknown ${centerfrequency}"
        echo "#sensor-status $(date +%s)000 1 .${mode}.bandwidth unknown ${bandwidth}"

      fi 

    fi
  done
}

change_mode_sensors () {
  from="$1"
  to="$2"

  if [ -z "${from}" -o -z "${to}" ] ; then
    kcpmsg -s mode -l warn "no static sensors to update"
    exit 0
  fi

  if [ -f ${CORR_CONFIG}/${from} ]  ; then
    echo "#sensor-status $(date +%s)000 1 .${from}.centerfrequency unknown \@"
    echo "#sensor-status $(date +%s)000 1 .${from}.channels unknown \@"
    echo "#sensor-status $(date +%s)000 1 .${from}.bandwidth unknown \@"
  fi

  if [ -f ${CORR_CONFIG}/${to} ] ; then
    echo "#sensor-status $(date +%s)000 1 .${to}.centerfrequency nominal \@"
    echo "#sensor-status $(date +%s)000 1 .${to}.channels nominal \@"
    echo "#sensor-status $(date +%s)000 1 .${to}.bandwidth nominal \@"
  fi

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
