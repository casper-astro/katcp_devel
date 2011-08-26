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
  if ps ax | grep -q bin/corr_katcp_interface ; then
    kcs_info "stopping corr"
    ${CORR_INIT_SCRIPT} stop
  fi

  if [ -n "$1" ] ; then

    if [ -e ${CONFIG}-$1 ] ; then
      if [ -h ${CONFIG} ] ; then
        kcs_debug "unlinking old configuration"
        rm -f ${CONFIG}
      fi
      kcs_debug "updating configuration to ${CONFIG}-${1}"
      ln -s ${CONFIG}-${1} ${CONFIG}
    else 
      kcs_error "no $1 configuration for corr"
      return 1
    fi

  fi

  kcs_debug "restarting corr"
  ${CORR_INIT_SCRIPT} start
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
