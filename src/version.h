#pragma once

#define RH_VERSION_MAJOR 2
<<<<<<< HEAD
#define RH_VERSION_MINOR 1
#define RH_VERSION_PATCH 0

#define RH_VERSION_LABEL "eldritch"
#define RH_VERSION_STRING "2.1.0"
=======
#define RH_VERSION_MINOR 0
#define RH_VERSION_PATCH 3

#define RH_VERSION_LABEL "eldritch"
#define RH_VERSION_STRING "2.0.3"
>>>>>>> main

#ifndef PUBLIC_BUILD
#define PUBLIC_BUILD 0
#endif

#if PUBLIC_BUILD
#define RH_BUILD_FLAVOR_STRING "public"
#else
#define RH_BUILD_FLAVOR_STRING "dev"
#endif

#define RH_BUILD_ID_STRING RH_VERSION_STRING "-" RH_VERSION_LABEL "-" RH_BUILD_FLAVOR_STRING

#define RH_MIN_REQUIRED_ASSET_PACK "1.1.9"