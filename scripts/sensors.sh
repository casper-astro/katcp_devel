
setup_sensors () {
  for config_file in ${CORR_CONFIG}/* ; do
    if [ -f ${config_file} ] ; then
      channels=$(grep ^n_chans ${config_file} 2> /dev/null | cut -f2 -d= | tr -d ' ' )
      if [ -n "${channels}" ] ; then
        sensor_name=${config_file##*/}.channels
        echo "#sensor-list ${sensor_name} number\_of\_channels none integer 0 65536"
        echo "#sensor-status $(date +%s)000 1 ${sensor_name} unknown ${channels}"
      fi 

      adc_clock=$(grep ^adc_clk ${config_file} 2> /dev/null | cut -f2 -d= | tr -d ' ' )
      coarse_channels=$(grep ^coarse_chans ${config_file} 2> /dev/null | cut -f2 -d= | tr -d ' ' )

      bandwidth=$(echo "${adc_clock:-400}/(${coarse_channels:-1}*2)" | bc -l)
      centerfrequency=$(echo "${bandwidth}/2" | bc -l)

      sensor_name=${config_file##*/}.centerfrequency
      echo "#sensor-list ${sensor_name} current\_center\_frequency Hz integer 0 500000000"
      echo "#sensor-status $(date +%s)000 1 ${sensor_name} unknown ${centerfrequency}"

      sensor_name=${config_file##*/}.bandwidth
      echo "#sensor-list ${sensor_name} bandwidth\_of\_current\_mode Hz integer 0 1000000000"
      echo "#sensor-status $(date +%s)000 1 ${sensor_name} unknown ${bandwidth}"

    fi
  done
}
