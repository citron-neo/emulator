string(TOUPPER "${arch}" arch)
file(READ "${input_file}" f_contents)
file(WRITE "${output_file}" "#if defined(ARCHITECTURE_${arch})\n${f_contents}\n#endif\n")
