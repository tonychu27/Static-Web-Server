make builder

docker-compose restart

sleep 2

docker exec -it sw_tester /testcase/run_hidden.sh demo 