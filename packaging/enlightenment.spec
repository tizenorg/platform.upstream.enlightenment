Name:           enlightenment
Version:        0.20.0
Release:        0
License:        BSD-2-Clause
Summary:        The Enlightenment wayland server
Url:            http://www.enlightenment.org/
Group:          Graphics/EFL
Source0:        enlightenment-%{version}.tar.bz2
Source1001:     enlightenment.manifest

BuildRequires:  doxygen
BuildRequires:  eet-tools
BuildRequires:  fdupes
BuildRequires:  gettext
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(ecore)
BuildRequires:  pkgconfig(ecore-con)
BuildRequires:  pkgconfig(ecore-evas)
BuildRequires:  pkgconfig(ecore-file)
BuildRequires:  pkgconfig(ecore-input)
BuildRequires:  pkgconfig(ecore-input-evas)
BuildRequires:  pkgconfig(ecore-ipc)
BuildRequires:  pkgconfig(edbus)
BuildRequires:  pkgconfig(edje)
BuildRequires:  pkgconfig(eet)
BuildRequires:  pkgconfig(efreet)
BuildRequires:  pkgconfig(eina)
BuildRequires:  pkgconfig(eio)
BuildRequires:  pkgconfig(evas)
BuildRequires:  pkgconfig(elementary)
BuildRequires:  pkgconfig(ice)
BuildRequires:  pkgconfig(libtbm)
BuildRequires:  pkgconfig(ttrace)
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(xdg-shell-server)
BuildRequires:  pkgconfig(scaler-server)
BuildRequires:  pkgconfig(transform-server)
BuildRequires:  pkgconfig(screenshooter-server)
BuildRequires:  pkgconfig(screenshooter-client)
BuildRequires:  pkgconfig(tizen-extension-server)
BuildRequires:  pkgconfig(wayland-tbm-server)
BuildRequires:  pkgconfig(ecore-drm)
Requires:       libwayland-extension-server


%description
Enlightenment is a window manager.

%package devel
Summary:        Development components for the enlightenment package
Group:          Development/Libraries
Requires:       %{name} = %{version}
Requires:       pkgconfig(tizen-extension-server)

%description devel
Development files for enlightenment

%prep
%setup -q -n %{name}-%{version}
cp %{SOURCE1001} .


%build
export CFLAGS+=" -fPIE "
export LDFLAGS+=" -pie "
%autogen \
      --enable-wayland \
      --enable-wl-drm \
      --enable-quick-init \
      --enable-light-e

make %{?_smp_mflags}

%install
%make_install

%find_lang enlightenment
%fdupes  %{buildroot}/%{_libdir}/enlightenment
%fdupes  %{buildroot}/%{_datadir}/enlightenment

%lang_package

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%license COPYING
%config %{_sysconfdir}/enlightenment/sysactions.conf
%{_bindir}/enlightenment*
%{_libdir}/enlightenment/*
%{_datadir}/enlightenment/*
%{_datadir}/xsessions/enlightenment.desktop
%{_sysconfdir}/xdg/menus/e-applications.menu
%{_datadir}/applications/enlightenment_filemanager.desktop
%{_sysconfdir}/dbus-1/system.d/org.enlightenment.wm.conf
%exclude /usr/share/enlightenment/data/config/profile.cfg
%exclude %{_bindir}/enlightenment_filemanager
%exclude %{_bindir}/enlightenment_imc
%exclude %{_bindir}/enlightenment_open
%exclude %{_bindir}/enlightenment_remote
%exclude %{_bindir}/enlightenment_start
%exclude %{_libdir}/enlightenment/utils/*
%exclude %{_libdir}/enlightenment/utils/
%exclude %{_datadir}/enlightenment/data/*
%exclude %{_datadir}/enlightenment/data/
%exclude %{_datadir}/enlightenment/doc/*
%exclude %{_datadir}/enlightenment/doc
%exclude %{_datadir}/xsessions/enlightenment.desktop
%exclude %{_sysconfdir}/xdg/menus/e-applications.menu
%exclude %{_datadir}/applications/enlightenment_filemanager.desktop

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_includedir}/enlightenment/*
%{_libdir}/pkgconfig/*.pc
