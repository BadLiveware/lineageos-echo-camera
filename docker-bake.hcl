variable "CHECKERS_FIRMWARE" {
  default = "."
}

variable "CHECKERS_BUILD_ID" {
  default = "checkers"
}

variable "CROWN_FIRMWARE" {
  default = "."
}

variable "CROWN_BUILD_ID" {
  default = "crown"
}

target "checkers" {
  context    = "."
  dockerfile = "Dockerfile"
  target     = "artifacts"
  platforms  = ["linux/amd64"]

  contexts = {
    firmware = CHECKERS_FIRMWARE
  }

  args = {
    LINEAGE_CACHE_ID = "lineageos-${CHECKERS_BUILD_ID}"
  }

  output = ["type=local,dest=dist/${CHECKERS_BUILD_ID}"]
}

target "crown-checksums" {
  context    = "."
  dockerfile = "Dockerfile"
  target     = "crown-artifacts"
  platforms  = ["linux/amd64"]

  contexts = {
    firmware = CROWN_FIRMWARE
  }

  args = {
    CROWN_CACHE_ID   = "lineageos-${CROWN_BUILD_ID}"
    CROWN_BUILD_MODE = "checksums"
  }

  output = ["type=local,dest=dist/${CROWN_BUILD_ID}-proprietary-review"]
}

target "crown-modules" {
  context    = "."
  dockerfile = "Dockerfile"
  target     = "crown-artifacts"
  platforms  = ["linux/amd64"]

  contexts = {
    firmware = CROWN_FIRMWARE
  }

  args = {
    CROWN_CACHE_ID   = "lineageos-${CROWN_BUILD_ID}"
    CROWN_BUILD_MODE = "modules"
  }

  output = ["type=local,dest=dist/${CROWN_BUILD_ID}-modules"]
}

target "crown" {
  context    = "."
  dockerfile = "Dockerfile"
  target     = "crown-artifacts"
  platforms  = ["linux/amd64"]

  contexts = {
    firmware = CROWN_FIRMWARE
  }

  args = {
    CROWN_CACHE_ID   = "lineageos-${CROWN_BUILD_ID}"
    CROWN_BUILD_MODE = "full"
  }

  output = ["type=local,dest=dist/${CROWN_BUILD_ID}"]
}
