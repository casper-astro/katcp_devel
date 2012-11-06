export CORR_CONFIG=/etc/corr/
export CORR_INIT_SCRIPT=/etc/init.d/corr
export KATCP_SERVER=localhost:1235
export CORR_MAPPING=/var/run/corr/antenna_mapping
export MAIN_MAPPING=/var/lib/kcs/mapping

kcs_check_timeout()
{
  if [ $(date +%s) -gt $1 ] ; then
    kcpmsg -l error -s script "request to corr timed out"
    return 1
  fi
}

kcs_set_frequency () {
  kcpcmd -i -k nb-set-cf $1 | ( while read cmd stat first rest ; do if [ "$cmd" = "!nb-set-cf" -a "${stat}" = "ok" ] ; then echo "#sensor-list .centerfrequency current\_selected\_center\_frequency Hz integer 0 1000000000" ; echo "#sensor-status $(date +%s)000 1 .centerfrequency nominal ${first}" ; fi ; done)
}

kcs_input_to_index () {
  echo $[$(echo $1 | tr -d -c [:digit:])*2+$(echo $1 | tr -dc xy | tr xy 01)]
}

kcs_index_to_input () {
  echo $[i/2]$(echo $[i%2] | tr 01 xy)
}

kcs_debug () {
  kcpmsg -l debug -s script "$1"
}

kcs_warn () {
  kcpmsg -l warn -s script "$1"
}

kcs_error () {
  kcpmsg -l error -s script "$1"
  return 1
}

kcs_info () {
  kcpmsg -l info -s script "$1"
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

    if [ ! -e ${CORR_CONFIG}/${1} ] ; then
      kcs_error "no $1 configuration for corr at ${CORR_CONFIG}/${1}"
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
  value=$(grep ^${1} ${CORR_CONFIG}/${KATCP_MODE} 2> /dev/null | cut -f2 -d= | tr -d ' ' )
  if [ -z "${value}" ] ; then
    kcs_error "unable to locate ${1} in ${CORR_CONFIG}/${KATCP_MODE}"
  fi

  kcs_debug "${1} maps to ${value}"

  export $1=${value}
}

kcs_load_config () {
  if [ $# -lt 1 ] ; then
    config_file=${CORR_CONFIG}/${KATCP_MODE}
  else
    config_file=${CORR_CONFIG}/$1
  fi

  eval $(grep -v '^ *#' ${config_file} | grep \=  | sed -e 's/=/ = /' | (while read label sep value ; do echo var_${label}=${value}\; ; done) ; echo echo )

}


