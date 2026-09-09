Name:           md-viewer
Version:        0.1.0
Release:        1%{?dist}
Summary:        Lightweight Markdown viewer for technical docs

License:        MIT
URL:            https://example.org/md-viewer
Source0:        %{name}-%{version}.tar.xz

BuildRequires:  gcc
BuildRequires:  meson >= 0.63
BuildRequires:  ninja-build
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(gtk+-3.0) >= 3.24

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

%check
%meson_test

%files
%license LICENSE
%doc README.md CHANGELOG.md
%{_bindir}/md-viewer

%changelog
* Wed Sep 09 2026 jimipohlers <jimipohlers@outlook.de> - 0.1.0-1
- Initial package