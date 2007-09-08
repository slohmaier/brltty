# autoconf input for Objective Caml programs
# Copyright (C) 2001 Jean-Christophe Filli�tre
#   from a first script by Georges Mariano 
# 
# Modified to be an autoconf m4 function in 2006
# for BRLTTY [http://mielke.cc/brltty/]
# by Dave Mielke <dave@mielke.cc>
# 
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License version 2, as published by the Free Software Foundation.
# 
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# 
# See the GNU Library General Public License version 2 for more details
# (enclosed in the file LGPL).

# the script generated by autoconf from this input will set the following
# variables:
#   OCAMLC        "ocamlc" if present in the path, or a failure
#                 or "ocamlc.opt" if present with same version number as ocamlc
#   OCAMLOPT      "ocamlopt" (or "ocamlopt.opt" if present), or "no"
#   OCAMLMKLIB      "ocamlkmlib", or "no"
#   OCAMLBEST     either "byte" if no native compiler was found, 
#                 or "opt" otherwise
#   OCAMLDEP      "ocamldep"
#   OCAMLLEX      "ocamllex" (or "ocamllex.opt" if present)
#   OCAMLYACC     "ocamlyac"
#   OCAMLLIB      the path to the ocaml standard library
#   OCAMLVERSION  the ocaml version number
#   OCAMLWEB      "ocamlweb" (not mandatory)
#   OCAMLWIN32    "yes"/"no" depending on Sys.os_type = "Win32"

AC_DEFUN([BRLTTY_CAML_BINDINGS], [dnl
# Check for Ocaml compilers

# we first look for ocamlc in the path; if not present, we fail
AC_CHECK_PROG(OCAMLC,ocamlc,ocamlc,no)
if test "$OCAMLC" = no ; then
    AC_MSG_WARN([Cannot find ocamlc.])
    CAML_OK=false
else
    # checking for ocamlmklib
    AC_CHECK_PROG(OCAMLMKLIB,ocamlmklib,ocamlmklib,no)
    if test "$OCAMLMKLIB" = no ; then
        AC_MSG_WARN([Cannot find ocamlmklib.])
        CAML_OK=false
    else    
        CAML_OK=true
        # we extract Ocaml version number and library path
        OCAMLVERSION=`$OCAMLC -v | sed -n -e 's|.*version *\(.*\)$|\1|p' `
        AC_MSG_NOTICE([OCaml version is $OCAMLVERSION])

        OCAMLLIB=`$OCAMLC -v | tail -1 | cut -f 4 -d " "`
        AC_MSG_NOTICE([OCaml library path is $OCAMLLIB])

        # then we look for ocamlopt; if not present, we issue a warning
        # if the version is not the same, we also discard it
        # we set OCAMLBEST to "opt" or "byte", whether ocamlopt is available or not
        AC_CHECK_PROG(OCAMLOPT,ocamlopt,ocamlopt,no)
        OCAMLBEST=byte
        OCAML_NCLIB=
        if test "$OCAMLOPT" = no ; then
            AC_MSG_WARN([Cannot find ocamlopt; bytecode compilation only.])
        else
            AC_MSG_CHECKING(ocamlopt version)
            TMPVERSION=`$OCAMLOPT -v | sed -n -e 's|.*version *\(.*\)$|\1|p' `
            if test "$TMPVERSION" != "$OCAMLVERSION" ; then
                AC_MSG_RESULT(differs from ocamlc; ocamlopt discarded.)
                OCAMLOPT=no
            else
                AC_MSG_RESULT(ok)
                OCAMLBEST=opt
                OCAML_NCLIB="\$(OCAML_LIB).cmxa"
            fi
        fi

        # checking for ocamlc.opt
        AC_CHECK_PROG(OCAMLCDOTOPT,ocamlc.opt,ocamlc.opt,no)
        if test "$OCAMLCDOTOPT" != no ; then
            AC_MSG_CHECKING(ocamlc.opt version)
            TMPVERSION=`$OCAMLCDOTOPT -v | sed -n -e 's|.*version *\(.*\)$|\1|p' `
            if test "$TMPVERSION" != "$OCAMLVERSION" ; then
                AC_MSG_RESULT(differs from ocamlc; ocamlc.opt discarded.)
            else
                AC_MSG_RESULT(ok)
                OCAMLC=$OCAMLCDOTOPT
            fi
        fi

        # checking for ocamlopt.opt
        if test "$OCAMLOPT" != no ; then
            AC_CHECK_PROG(OCAMLOPTDOTOPT,ocamlopt.opt,ocamlopt.opt,no)
            if test "$OCAMLOPTDOTOPT" != no ; then
                AC_MSG_CHECKING(ocamlc.opt version)
                TMPVER=`$OCAMLOPTDOTOPT -v | sed -n -e 's|.*version *\(.*\)$|\1|p' `
                if test "$TMPVER" != "$OCAMLVERSION" ; then
                    AC_MSG_RESULT(differs from ocamlc; ocamlopt.opt discarded.)
                else
                    AC_MSG_RESULT(ok)
                    OCAMLOPT=$OCAMLOPTDOTOPT
                fi
            fi
        fi

        # platform
        AC_MSG_CHECKING(platform)
        if echo "let _ = Sys.os_type;;" | ocaml | grep -q Win32; then
            AC_MSG_RESULT(Win32)
            OCAMLWIN32=yes
            OCAML_CLIBS=libbrlapi.a
        elif echo "let _ = Sys.os_type;;" | ocaml | grep -q Cygwin; then
            AC_MSG_RESULT(Cygwin)
            OCAMLWIN32=yes
            OCAML_CLIBS=libbrlapi.a
        else
            AC_MSG_RESULT(Unix)
            OCAMLWIN32=no
            OCAML_CLIBS="libbrlapi.a dllbrlapi.so"
        fi
    
        # checking for ocamlfindlib
        AC_CHECK_PROG(OCAMLFIND,ocamlfind,ocamlfind,no)
        if test "$OCAMLFIND" = ocamlfind; then
            OCAMLC='ocamlfind ocamlc'
            if test "$OCAMLOPT" = ocamlopt; then
                OCAMLOPT='ocamlfind ocamlopt'
            fi
            OCAML_INSTALL_TARGET=install-with-findlib
            OCAML_UNINSTALL_TARGET=uninstall-without-findlib
        else
            OCAML_INSTALL_TARGET=install-without-findlib
            OCAML_UNINSTALL_TARGET=uninstall-without-findlib
            AC_MSG_WARN([Cannot find ocamlfind.])
            AC_MSG_WARN([BrlAPI Caml bindings will be compiled but not installed.])
        fi
    fi
fi

# substitutions to perform
AC_SUBST(OCAMLC)
AC_SUBST(OCAMLOPT)
AC_SUBST(OCAMLMKLIB)
AC_SUBST(OCAMLBEST)
AC_SUBST(OCAMLVERSION)
AC_SUBST(OCAMLLIB)
AC_SUBST(OCAMLWIN32)
AC_SUBST(OCAML_CLIBS)
AC_SUBST(OCAML_NCLIB)
AC_SUBST(OCAMLFIND)
AC_SUBST(OCAML_INSTALL_TARGET)
AC_SUBST(OCAML_UNINSTALL_TARGET)
])
