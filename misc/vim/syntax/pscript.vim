syn	keyword	pssDirective	function var for while return break continue in if \$global
syn keyword pssPredifined   undefined import dict print insmod
syn match	pssId			/[\$_a-zA-Z][\$_a-zA-Z0-9\.]*/
syn match   pssNumber		 "\<\(0[bB][0-1]\+\|0[0-7]*\|0[xX]\x\+\|\d\(\d\|_\d\)*\)[lL]\=\>"
syn match   pssNumber		 "\(\<\d\(\d\|_\d\)*\.\(\d\(\d\|_\d\)*\)\=\|\.\d\(\d\|_\d\)*\)\([eE][-+]\=\d\(\d\|_\d\)*\)\=[fFdD]\="
syn	match	pssComment		/#.*/
syn match	pssComment		/\/\/.*/
syn region	pssComment		start=/\/\*/	end=/\*\//
syn	match	pssServlet		/[_a-zA-Z][_a-zA-Z0-9]*/ contained
"syn region	pssBlock		start=/{/ end=/}/ transparent contains=pssServlet,pssString,pssGraphviz,pssSourcePipe,pssBlock, pssComment, pssDirective, pssServletDef, pssPipeName
syn	region	pssString		start=/"/ skip=+\\\\\|\\"+ end=/"/ 
"syn	region	pssPipeName		start=/"/ skip=+\\\\\|\\"+ end=/"/ contained
"syn region	pssGraphviz		start=/@\[/ end=/\]/ contained
"syn match	pssSourcePipe	/()/ contained
"syn match	pssServletDef	/:=/ nextgroup=pssStringExpr contained 
"syn match   pssOperator		/[+-\*\/%]/ contained
syn region	pssStringExpr	start=// end=/\(@\[\|;\|#\|\/\/\|\/#\|$\)\@=/ contained contains=pssNumber,pssId,pssOperator,pssString

hi def link pssDirective	Keyword
hi def link pssString		String
hi def link pssGraphviz		Comment
hi def link pssPredifined   Special
hi def link pssNumber		Number
hi def link pssComment		Comment
hi def link pssServlet		Special
hi def link pssId			Identifier
hi def link pssSourcePipe	Constant
hi def link pssPipeName		Constant	
