%bcond_with wayland
%bcond_with x

Name:           enlightenment
Version:        0.18.7
Release:        0
License:        BSD 2-clause
Summary:        The Enlightenment window manager
Url:            http://www.enlightenment.org/
Group:          Graphics/EFL
Source0:        enlightenment-%{version}.tar.bz2
Source1001: 	enlightenment.manifest
BuildRequires:  doxygen
BuildRequires:  fdupes
BuildRequires:  gettext
BuildRequires:  pam-devel
BuildRequires:  pkgconfig(alsa)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(ecore)
BuildRequires:  pkgconfig(ecore-con)
BuildRequires:  pkgconfig(ecore-evas)
BuildRequires:  pkgconfig(ecore-file)
BuildRequires:  pkgconfig(ecore-input)
BuildRequires:  pkgconfig(ecore-input-evas)
BuildRequires:  pkgconfig(ecore-ipc)

%if %{with wayland}
%endif

%if %{with x}
BuildRequires:  pkgconfig(ecore-x)
BuildRequires:  pkgconfig(x11)
%else
ExclusiveArch:	
%endif

BuildRequires:  pkgconfig(edbus)
BuildRequires:  pkgconfig(edje)
BuildRequires:  pkgconfig(eet)
BuildRequires:  pkgconfig(eeze)
BuildRequires:  pkgconfig(efreet)
BuildRequires:  pkgconfig(eina)
BuildRequires:  pkgconfig(eio)
BuildRequires:  pkgconfig(evas)
BuildRequires:  pkgconfig(ice)
BuildRequires:  pkgconfig(libudev)
BuildRequires:  pkgconfig(udev)
BuildRequires:  pkgconfig(xext)
BuildRequires:  pkgconfig(xcb-keysyms)
BuildRequires:  eet-tools
BuildRequires:  eldbus-devel
BuildRequires:  embryo-devel
# elementary
# emotion
# ephysics


%if !%{with x}
ExclusiveArch:
%endif


%description
Enlightenment is a window manager.

%package devel
Summary:        Development components for the enlightenment package
Group:          Development/Libraries
Requires:       %{name} = %{version}

%description devel
Development files for enlightenment

%prep
%setup -q -n %{name}-%{version}
cp %{SOURCE1001} .


%build

%reconfigure \
    --enable-device-udev \
	    --enable-mount-eeze  \
    --enable-comp \
    --enable-wayland-only \
    --enable-wayland-clients \
    #eol

make %{?_smp_mflags} -j1

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
%{_sysconfdir}/xdg/menus/enlightenment.menu
%{_datadir}/applications/enlightenment_filemanager.desktop
/usr/lib/systemd/user/e18.service

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_includedir}/enlightenment/*
%{_libdir}/pkgconfig/*.pc



%changelog
