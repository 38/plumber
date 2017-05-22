syn keyword ptypeKeyword         package import export type alias packed
syn keyword ptypePrimitive       int8 uint8 int16 uint16 int32 uint32 int64 uint64 double float address hashmap
syn match	ptypeId              /[_a-zA-Z][_a-zA-Z0-9]*/
syn match   ptypeNumber          "\<\(0[bB][0-1]\+\|0[0-7]*\|0[xX]\x\+\|\d\(\d\|_\d\)*\)[lL]\=\>"
syn	match	ptypeComment         /#.*/
syn match	ptypeComment         /\/\/.*/
syn region	ptypeComment         start=/\/\*/	end=/\*\//

hi def link ptypeKeyword Keyword
hi def link ptypeId      Identifier
hi def link ptypeNumber  Number
hi def link ptypeComment Comment
hi def link ptypePrimitive Special  
