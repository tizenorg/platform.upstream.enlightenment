%bcond_with x
%bcond_with wayland

Name:           enlightenment
Version:        0.19.0
Release:        0
License:        BSD-2-Clause
Summary:        The Enlightenment window manager
Url:            http://www.enlightenment.org/
Group:          Graphics/EFL
Source0:        enlightenment-%{version}.tar.bz2
Source1001:     enlightenment.manifest
Source1002:     enlightenment.service
BuildRequires:  doxygen
BuildRequires:  eet-tools
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
BuildRequires:  pkgconfig(edbus)
BuildRequires:  pkgconfig(edje)
BuildRequires:  pkgconfig(eet)
BuildRequires:  pkgconfig(eeze)
BuildRequires:  pkgconfig(efreet)
BuildRequires:  pkgconfig(eina)
BuildRequires:  pkgconfig(eio)
BuildRequires:  pkgconfig(evas)
BuildRequires:  pkgconfig(elementary)
BuildRequires:  pkgconfig(ice)
BuildRequires:  pkgconfig(libudev)
BuildRequires:  pkgconfig(udev)
%if %{with x}
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xext)
BuildRequires:  pkgconfig(xcb-keysyms)
BuildRequires:  pkgconfig(ecore-x)
%else
BuildRequires:  pkgconfig(wayland-server)
%endif
Requires:       monotype-fonts


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
cp %{SOURCE1002} .


%build
%autogen \
%if %{with wayland}
      --enable-wayland-only \
      --enable-wl-drm \
      --disable-shot \
      --disable-xkbswitch \
      --disable-conf-randr \
      --disable-wl-x11 \
%endif
      --enable-mount-eeze

make %{?_smp_mflags}

%install
%make_install

mkdir -p %{buildroot}%{_prefix}/lib/systemd/system/
mkdir -p %{buildroot}%{_prefix}/lib/systemd/system/graphical.target.wants
mkdir -p %{buildroot}%{_sysconfdir}/dbus-1/system.d
install -m 0644 %{SOURCE1002} %{buildroot}%{_prefix}/lib/systemd/system/
ln -sf ../enlightenment.service %{buildroot}%{_prefix}/lib/systemd/system/graphical.target.wants
rm -f %{buildroot}%{_prefix}/lib/systemd/user/enlightenment.service

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
%{_prefix}/lib/systemd/system/enlightenment.service
%{_prefix}/lib/systemd/system/graphical.target.wants/enlightenment.service
%{_sysconfdir}/dbus-1/system.d/org.enlightenment.wm.conf
%exclude /usr/share/enlightenment/data/config/profile.cfg

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_includedir}/enlightenment/*
%{_libdir}/pkgconfig/*.pc
