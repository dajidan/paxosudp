#!/bin/sh

if [ $# -lt 2 ] ; then
  echo "usage: udptest.sh [#learners] [#outstanding]"
  exit 1
fi

N="$1"
OST="$2"

BWM_DIR="/home/danghu/log-bwm"
EXEC_DIR="/home/danghu/paxosudp/build/sample"
CONF_DIR="/home/danghu/paxosudp/scripts"
#start acceptors & proposer
ssh node81 "nohup $EXEC_DIR/acceptor 0 $CONF_DIR/paxos.conf > experiment_log/acceptor0.log 2>&1 &"
ssh node82 "nohup $EXEC_DIR/acceptor 1 $CONF_DIR/paxos.conf > experiment_log/acceptor1.log 2>&1 &"
ssh node83 "nohup $EXEC_DIR/acceptor 2 $CONF_DIR/paxos.conf > experiment_log/acceptor2.log 2>&1 &"
ssh node84 "nohup $EXEC_DIR/proposer 0 $CONF_DIR/paxos.conf > experiment_log/proposer0.log 2>&1 &"

#start bwm-ng on acceptors & proposer
ssh node81 "nohup bwm-ng $CONF_DIR/bwm-ng.conf > $BWM_DIR/$N/acceptor0.csv 2>&1 &"
ssh node82 "nohup bwm-ng $CONF_DIR/bwm-ng.conf > $BWM_DIR/$N/acceptor1.csv 2>&1 &"
ssh node83 "nohup bwm-ng $CONF_DIR/bwm-ng.conf > $BWM_DIR/$N/acceptor2.csv 2>&1 &"
ssh node84 "nohup bwm-ng $CONF_DIR/bwm-ng.conf > $BWM_DIR/$N/proposer0.csv 2>&1 &"
            nohup bwm-ng $CONF_DIR/bwm-ng.conf > $BWM_DIR/$N/client0.csv 2>&1 &

#ssh node88 "nohup $EXEC_DIR/learner 0 $CONF_DIR/paxos.conf > experiment_log/learner0.log 2>&1 &"
#ssh node88 "nohup bwm-ng $CONF_DIR/bwm-ng.conf > $BWM_DIR/$N/learner0.csv 2>&1 &"

#run the client
$EXEC_DIR/client $CONF_DIR/paxos.conf 0 $OST 8192 0 
mv client0-$OST-8192B.csv udp-$N-$OST.csv

echo "Terminate Acceptors and Proposers"

ssh node81 "pkill acceptor"
ssh node82 "pkill acceptor"
ssh node83 "pkill acceptor"
ssh node84 "pkill proposer"
#ssh node88 "pkill learner"
#for ((i=1; i<=N; i++ ))
#  do
#  c=$[$i+1]
#  ssh node$c "pkill learner"
#  done
echo "Experiment end successfully"