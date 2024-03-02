Name:       lava-ocf-adaptor
Version:    1.0.0
Release:    1%{?dist}
Summary:    adaptation to lava based on open cas framework
License:    BSD

Source0:    %{name}-%{version}.tar.gz

# if BuildRequires and Requires are placed after description, BuildRequires and Requires are invalid
BuildRequires:    zlib-devel
Requires:   zlib

%description
Product Name: Kunpeng BoostKit
Product Version: 24.0.RC2
Component Name: BoostKit-OCF
Component Version: %{version}-%{release}
Component AppendInfo: kunpeng

%prep
%setup -a 0 -c -q

%build
cd ocf/lava_ocf_adaptor
make distclean
make -j16

%install
if [-d %{buildroot}]; then
	rm -rf %{buildroot}
fi
cd ocf/lava_ocf_adaptor
make install DESTDIR=%{buildroot}

%pre

%post

%preun

%postun

%files
%defattr(-, root, root)
%attr(0644, root, root) /usr/include/*
%attr(0755, root, root) /usr/lib64/*