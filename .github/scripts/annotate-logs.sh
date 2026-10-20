#!/usr/bin/env bash
# Turn the tail of each captured log in ci-logs/ into error annotations, so a
# failure's cause shows up in the run summary (and via the checks API)
# without opening the raw job log.
shopt -s nullglob
for f in ci-logs/*.log; do
  name=$(basename "$f" .log)
  # error-looking lines first, then the plain tail
  { grep -nE "error|Error|ERROR|FAIL|fatal|undefined reference|Could not|not found" "$f" | tail -n 40
    echo "----- last lines -----"; tail -n 40 "$f"; } > ci-logs/.excerpt
  # annotations are limited in size: send the excerpt in chunks of 20 lines
  split -l 20 ci-logs/.excerpt ci-logs/.chunk-
  for c in ci-logs/.chunk-*; do
    msg=$(sed -e 's/%/%25/g' -e 's/\r//g' "$c" | awk '{printf "%s%%0A", $0}')
    echo "::error title=${name} log::${msg}"
    rm -f "$c"
  done
done
