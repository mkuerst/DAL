for file in /home/mihi/Desktop/DAL/results2112/orig/*/mem/; do
    dir=$(dirname "$file")
    base=$(basename "$file" .csv)
    new_base="mem_2nodes"
    new_name="${dir}/${new_base}"
    echo $dir 
    echo $base 
    echo $new_name
    # new_name="${dir}/nclients1_${base}.csv"
    mv "$file" "$new_name"
done