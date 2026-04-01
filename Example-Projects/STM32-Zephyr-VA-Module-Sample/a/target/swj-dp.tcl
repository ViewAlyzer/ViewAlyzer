# Fixed swj-dp.tcl - breaks hla newtap <-> swj_newdap recursion
# in Ubuntu's ST-patched OpenOCD 0.12.0

if [catch {transport select}] {
  echo "Error: unable to select a session transport. Can't continue."
  shutdown
}

proc swj_newdap {chip tag args} {
  if [using_jtag] {
    eval jtag newtap $chip $tag $args
  } elseif [using_swd] {
    eval swd newdap $chip $tag $args
  } elseif [using_hla] {
    # Use the raw Tcl command to avoid recursion
    eval "hla newtap" $chip $tag $args
  }
}
