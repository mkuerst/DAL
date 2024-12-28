# for file in /home/mihi/Desktop/DAL/results2112/*/spinlock/client/mem2n/mem_2n; do
#     dir=$(dirname "$file")
#     base=$(basename "$file" .csv)
#     new_base="mem2n"
#     new_name="${dir}/${new_base}"
#     echo ""
#     echo $file
#     echo ""
#     # echo $dir 
#     # echo ""
#     # echo $base 
#     # echo ""
#     # new_name="${dir}/nclients1_${base}.csv"
#     echo $new_name
#     mv "$file" "$new_name"
# done



for file in /home/mihi/Desktop/DAL/results2112/*/spinlock/client/*/mem2n/mem_2n/*; do
    parent_dir=$(dirname "$(dirname "$file")")
    echo ""
    echo $file
    echo ""
    echo $parent_dir

    mv "$file" "$parent_dir"
done

for dir in /home/mihi/Desktop/DAL/results2112/*/spinlock/client/*/mem2n/mem_2n; do
    rmdir "$dir"
done