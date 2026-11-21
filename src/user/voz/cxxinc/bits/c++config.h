// SOMA_241_VOZ: envoltorio de la configuracion de libstdc++. Desactiva las "extern template" para que std::string y
// compania se instancien dentro del programa (compilado sin excepciones) en vez de enlazar string-inst.o de libstdc++.a,
// que arrastra toda la maquinaria de excepciones. Solo hace falta de libstdc++.a lo que no son plantillas (arboles de std::map).
#include_next <bits/c++config.h>
#undef _GLIBCXX_EXTERN_TEMPLATE
#define _GLIBCXX_EXTERN_TEMPLATE 0
