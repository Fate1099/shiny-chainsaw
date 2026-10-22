Name:           md-viewer
Version:        0.1.0
Release:        1%{?dist}
Summary:        Lightweight Markdown viewer for technical docs

License:        MIT
URL:            https://example.org/md-viewer
Source0:        %{name}-%{version}.tar.xz

BuildRequires:  gcc
BuildRequires:  desktop-file-utils
BuildRequires:  meson >= 0.63
BuildRequires:  ninja-build
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(gtk+-3.0) >= 3.24
BuildRequires:  pkgconfig(libsoup-3.0)

Requires:       gtk3 >= 3.24
Requires:       glib2

%description
md-viewer is a pure, lightweight Markdown viewer written in C with GTK3.
It has no runtime dependency on external Markdown renderers; the parser is
implemented in-repo in C.

%prep
%autosetup

%build
%meson
%meson_build

%install
%meson_install
desktop-file-validate %{buildroot}%{_datadir}/applications/md-viewer.desktop

%check
meson test -C %{_vpath_builddir} %{_smp_mflags} --print-errorlogs

%post
update-desktop-database %{_datadir}/applications &> /dev/null || :
gtk-update-icon-cache -f -t %{_datadir}/icons/hicolor &> /dev/null || :
glib-compile-schemas %{_datadir}/glib-2.0/schemas &> /dev/null || :

%postun
update-desktop-database %{_datadir}/applications &> /dev/null || :
gtk-update-icon-cache -f -t %{_datadir}/icons/hicolor &> /dev/null || :
glib-compile-schemas %{_datadir}/glib-2.0/schemas &> /dev/null || :

%files
%license LICENSE
%doc README.md CHANGELOG.md
%{_bindir}/md-viewer
%{_datadir}/applications/md-viewer.desktop
%{_datadir}/icons/hicolor/scalable/apps/md-viewer.svg
%{_datadir}/glib-2.0/schemas/org.example.mdviewer.gschema.xml

%changelog
* Wed Sep 09 2026 jimipohlers <jimipohlers@outlook.de> - 0.1.0-1
- Initial package