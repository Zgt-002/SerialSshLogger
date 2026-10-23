isEmpty(LIBSSH_ROOT): LIBSSH_ROOT = $$(LIBSSH_ROOT)
isEmpty(LIBSSH_ROOT) {
    macx:equals(QT_ARCH, arm64): LIBSSH_ROOT = $$PWD/lib/macos-arm64
    win32: LIBSSH_ROOT = $$PWD/lib/windows-x64
}
!isEmpty(LIBSSH_ROOT) {
    INCLUDEPATH += $$quote($$LIBSSH_ROOT/include)
    win32-g++ {
        LIBS += $$quote($$LIBSSH_ROOT/lib/libssh.dll.a)
    } else {
        win32-msvc:!exists($$LIBSSH_ROOT/lib/ssh.lib) {
            error("The bundled Windows library is MinGW x64. Select a Qt MinGW kit or set LIBSSH_ROOT to an MSVC build.")
        }
        LIBS += -L$$quote($$LIBSSH_ROOT/lib) -lssh
    }
    unix: QMAKE_RPATHDIR += $$quote($$LIBSSH_ROOT/lib)
} else {
    unix:!macx {
        CONFIG += link_pkgconfig
        PKGCONFIG += libssh
    } else {
        error("Set LIBSSH_ROOT to the libssh installation directory.")
    }
}
