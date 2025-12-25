#!/bin/sh

# Validación de argumentos
if [ $# -ne 2 ]; then
    echo "Error: Se requieren 2 argumentos." >&2
    echo "Uso: $0 <filesdir> <searchstr>" >&2
    exit 1
fi

filesdir=$1
searchstr=$2


if [ ! -d "$filesdir" ]; then
    echo "Error: '$filesdir' no existe o no es un directorio." >&2
    exit 1
fi

# Contar número de archivos regulares en el directorio (recursivo)
num_files=$(find "$filesdir" -type f 2>/dev/null | wc -l)

num_matches=$(grep -r "$searchstr" "$filesdir" 2>/dev/null | wc -l)

echo "The number of files are ${num_files} and the number of matching lines are ${num_matches}"

# Imprimir cada archivo y su contenido
find "$filesdir" -type f 2>/dev/null | while read file; do
    echo ""
    echo "Archivo: $file. Contenido:"
    cat "$file"
done