
%{
# include  "config.tab.hpp"
# include  <string.h>
%}

%option noyywrap

%%

"#".* { ; }

  /* Keywords. */
"bus"    { return K_bus; }
"device" { return K_device; }
"name"   { return K_name; }
"port"   { return K_port; }

  /* Skip white space */
[ \t\r\n] { ; }

  /* Integers are tokens. */
[0123456789]+ {
      configlval.integer = strtoul(yytext,0,0);
      return INTEGER;
  }

  /* strings are surrounded by quotes. */
\".*\" {
      configlval.text = strdup(yytext+1);
	/* Remove trailing quote. */
      configlval.text[strlen(yytext+1)-1] = 0;
      return STRING;
  }

  /* Single-character tokens */
. { return yytext[0]; }

%%
