variable "project" {
  type    = string
  default = "lmkr"
}

variable "aws_region" {
  type    = string
  default = "us-east-1"
}

# your laptop's public IP in CIDR form: "138.51.81.94/32"
variable "allowed_cidr" {
  type = string
}

variable "db_name" {
  type    = string
  default = "seismics"
}

variable "db_user" {
  type    = string
  default = "simrah"
}

variable "db_password" {
  type      = string
  sensitive = true
}
