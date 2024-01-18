../src/stop.sh
../src/vstart.sh --new --without-dashboard --memstore
./bin/ceph osd pool create rbd 
./bin/rbd -p rbd create demo_image1 --size 10M
./bin/rbd -p rbd create demo_image2 --size 10M
pushd ../src/nvmeof/gateway
make down
docker-compose up -d --scale nvmeof=2 nvmeof
sleep 10
docker ps
GW1=ceph-nvmeof_nvmeof_1
GW2=ceph-nvmeof_nvmeof_2
GW1_NAME=$(docker ps -aqf "name=${GW1}")
GW2_NAME=$(docker ps -aqf "name=${GW2}")
GW1_IP="$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$GW1_NAME")"
GW2_IP="$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$GW2_NAME")"

docker-compose  run --rm nvmeof-cli --server-address $GW1_IP --server-port 5500 create_bdev --pool rbd --image demo_image1 --bdev demo_bdev1
docker-compose  run --rm nvmeof-cli --server-address $GW1_IP --server-port 5500 create_bdev --pool rbd --image demo_image2 --bdev demo_bdev2
docker-compose  run --rm nvmeof-cli --server-address $GW1_IP --server-port 5500 create_subsystem --subnqn "nqn.2016-06.io.spdk:cnode1" -a -t
docker-compose  run --rm nvmeof-cli --server-address $GW1_IP --server-port 5500 add_namespace --subnqn "nqn.2016-06.io.spdk:cnode1" --bdev demo_bdev1  -a 1
docker-compose  run --rm nvmeof-cli --server-address $GW1_IP --server-port 5500 add_namespace --subnqn "nqn.2016-06.io.spdk:cnode1" --bdev demo_bdev2  -a 2
docker-compose  run --rm nvmeof-cli --server-address $GW1_IP --server-port 5500 create_listener --subnqn "nqn.2016-06.io.spdk:cnode1" --gateway-name $GW1_NAME --traddr $GW1_IP --trsvcid 4420
docker-compose  run --rm nvmeof-cli --server-address $GW2_IP --server-port 5500 create_listener --subnqn "nqn.2016-06.io.spdk:cnode1" --gateway-name $GW2_NAME --traddr $GW2_IP --trsvcid 4420
docker-compose  run --rm nvmeof-cli --server-address $GW1_IP --server-port 5500 add_host --subnqn "nqn.2016-06.io.spdk:cnode1" --host "*"
docker-compose  run --rm nvmeof-cli --server-address $GW1_IP --server-port 5500 get_subsystems
docker-compose  run --rm nvmeof-cli --server-address $GW2_IP --server-port 5500 get_subsystems
popd
