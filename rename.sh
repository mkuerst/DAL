for file in /home/mihi/Desktop/DAL/results2112/orig/*/mem_2nodes; do
    dir=$(dirname "$file")
    base=$(basename "$file" .csv)
    new_base="mem_2n"
    new_name="${dir}/${new_base}"
    echo ""
    echo $file
    echo ""
    # echo $dir 
    # echo ""
    # echo $base 
    # echo ""
    echo $new_name
    # new_name="${dir}/nclients1_${base}.csv"
    mv "$file" "$new_name"
done