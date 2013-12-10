if [ -z $3 ]
then
    echo "usage: $0 graph.txt bsm.list week/day"
    exit 1
fi
for w in 10 100 1000
do
    echo $w
    python analyze.py $1 $2 $w > $3-$w.results.txt
done

