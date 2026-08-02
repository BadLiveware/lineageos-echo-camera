variable "CHECKERS_FIRMWARE" {
  default = "."
}

variable "CHECKERS_BUILD_ID" {
  default = "checkers"
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
