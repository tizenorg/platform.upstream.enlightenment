Name:           enlightenment
Version:        0.16.999.77927
Release:        1
License:        BSD 2-clause
Summary:        The Enlightenment window manager
Url:            http://www.enlightenment.org/
Group:          Graphics/X11
Source0:        enlightenment-%{version}.tar.gz
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

Source1:	e17.service
%description
Enlightenment is a window manager.

%package devel
Summary:        Development components for the enlightenment package
Group:          Development/Libraries
Requires:       %{name} = %{version}

%description devel
Development files for enlightenment

%prep
%setup -q


%build

%configure  --enable-device-udev \
	    --enable-mount-eeze \
	    --disable-comp
make %{?_smp_mflags}

%install
%make_install


mkdir -p %{buildroot}%{_unitdir_user}/core-efl.target.wants

cat > %{buildroot}%{_unitdir_user}/core-efl.target << EOF
[Unit]
Description=EFL Target
Wants=xorg.target
EOF

install -m 0644 %SOURCE1 %{buildroot}%{_unitdir_user}/
ln -s ../e17.service %{buildroot}%{_unitdir_user}/core-efl.target.wants/e17.service

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
/usr/share/applications/enlightenment_filemanager.desktop
%{_unitdir_user}/core-efl.target.wants/*.service
%{_unitdir_user}/core-efl.target
%{_unitdir_user}/*.service

%files devel
%defattr(-,root,root,-)
%{_includedir}/enlightenment/*
%{_libdir}/pkgconfig/*.pc



%changelog
