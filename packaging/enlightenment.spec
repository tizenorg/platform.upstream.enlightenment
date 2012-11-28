Name:           enlightenment
Version:        0.17.0
Release:        1
License:        BSD 2-clause
Summary:        The Enlightenment window manager
Url:            http://www.enlightenment.org/
Group:          Graphics/X11
Source0:        enlightenment-%{version}-alpha6.tar.bz2
BuildRequires:  doxygen
#BuildRequires:  valgrind
BuildRequires:  fdupes
BuildRequires:  gettext
BuildRequires:  pam-devel
BuildRequires:  pkgconfig(alsa)
#BuildRequires:  pkgconfig(librsvg-2.0)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(ecore)
BuildRequires:  pkgconfig(ecore-con)
BuildRequires:  pkgconfig(ecore-evas)
BuildRequires:  pkgconfig(ecore-file)
BuildRequires:  pkgconfig(ecore-input)
BuildRequires:  pkgconfig(ecore-input-evas)
BuildRequires:  pkgconfig(ecore-ipc)
BuildRequires:  pkgconfig(ecore-x)
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
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xext)
BuildRequires:  pkgconfig(xcb-keysyms)

# Patches auto-generated by git-buildpackage:
%description
Enlightenment is a window manager.

%package devel
Summary:        Development components for the enlightenment package
Group:          Development/Libraries
Requires:       %{name} = %{version}

%description devel
Development files for enlightenment

%prep
%setup -q -n %{name}-%{version}-alpha6


%build
%configure  --enable-device-udev \
	    --enable-mount-eeze \
	    --disable-comp
make %{?_smp_mflags}

%install
%make_install


%find_lang enlightenment
%fdupes  %{buildroot}/%{_libdir}/enlightenment
%fdupes  %{buildroot}/%{_datadir}/enlightenment


%files -f enlightenment.lang
%defattr(-,root,root,-)
%doc COPYING
%config %{_sysconfdir}/enlightenment/sysactions.conf
%{_bindir}/enlightenment*
%{_libdir}/enlightenment/*
%{_datadir}/enlightenment/*
%{_datadir}/xsessions/enlightenment.desktop
%{_sysconfdir}/xdg/menus/enlightenment.menu
%{_datadir}/applications/enlightenment_filemanager.desktop

%files devel
%defattr(-,root,root,-)
%{_includedir}/enlightenment/*
%{_libdir}/pkgconfig/*.pc



%changelog
