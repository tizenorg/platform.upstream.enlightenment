Name:           enlightenment
Version:        0.20.0
Release:        0
License:        BSD-2-Clause
Summary:        The Enlightenment wayland display server
Url:            http://www.enlightenment.org/
Group:          Graphics/EFL
Source0:        enlightenment-%{version}.tar.bz2
Source1001:     enlightenment.manifest

BuildRequires:  eet-tools
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(ecore)
BuildRequires:  pkgconfig(ecore-evas)
BuildRequires:  pkgconfig(ecore-file)
BuildRequires:  pkgconfig(ecore-input)
BuildRequires:  pkgconfig(edbus)
BuildRequires:  pkgconfig(edje)
BuildRequires:  pkgconfig(eet)
BuildRequires:  pkgconfig(eina)
BuildRequires:  pkgconfig(eio)
BuildRequires:  pkgconfig(evas)
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
BuildRequires:  pkgconfig(libtdm)
BuildRequires:  pkgconfig(gbm)
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
export CFLAGS+=" -fPIE -O1 -finstrument-functions -finstrument-functions-exclude-function-list=printf,fprintf,fclose,fopen,dladdr,trace_end,trace_begin,getchar,memset"
export LDFLAGS+=" -pie "
%if "%_repository" == "emulator32-wayland" || "%_repository" == "emulator64-wayland"
%autogen --enable-wayland --enable-wl-drm --enable-quick-init --disable-hwc
%else
%autogen --enable-wayland --enable-wl-drm --enable-quick-init
%endif

make %{?_smp_mflags}

%install
%make_install

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%license COPYING
%{_bindir}/enlightenment*
%{_libdir}/enlightenment/*
%{_datadir}/enlightenment/*
%{_sysconfdir}/dbus-1/system.d/org.enlightenment.wm.conf
%exclude %{_bindir}/enlightenment_remote
%exclude /usr/share/enlightenment/data/config/profile.cfg
%exclude %{_datadir}/enlightenment/data/*
%exclude %{_datadir}/enlightenment/data/

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_includedir}/enlightenment/*
%{_libdir}/pkgconfig/*.pc
