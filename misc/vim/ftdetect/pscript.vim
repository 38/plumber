fun! s:DetectPscript()
	if getline(1) =~# '^.*pscript\(\| .*\)$'
		set filetype=pscript
	endif
endfun
au BufRead,BufNewFile *.pss set filetype=pscript
au BufRead,BufNewFile * call s:DetectPscript()
