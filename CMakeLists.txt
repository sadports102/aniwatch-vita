cmake_minimum_required(VERSION 3.10)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(aniwatch C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "aniwatch")
set(VITA_TITLEID  "AVWA00001")
set(VITA_VERSION  "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2 -std=gnu11")

add_executable(${PROJECT_NAME} main.c)

target_link_libraries(${PROJECT_NAME}
  SDL2_ttf freetype
  SDL2_image webp jpeg png bz2 z
  webpdemux
  webp
  SDL2
  curl ssl crypto zstd z
  SceDisplay_stub SceCtrl_stub SceAudio_stub SceAudioIn_stub SceSysmodule_stub
  SceGxm_stub SceCommonDialog_stub SceAppMgr_stub SceTouch_stub SceHid_stub
  SceMotion_stub ScePower_stub SceProcessmgr_stub SceIofilemgr_stub
  SceNet_stub SceNetCtl_stub SceIme_stub
  m
  pthread
  SceAvPlayer_stub SceAudio_stub SceSysmodule_stub SceGxm_stub
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE res/font.ttf font.ttf
  FILE res/font_bold.ttf font_bold.ttf
  FILE res/cacert.pem cacert.pem
)
