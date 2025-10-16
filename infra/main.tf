terraform {
  required_version = ">= 1.6.0"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
    random = {
      source = "hashicorp/random"
      version = "~> 3.6"
    }
  }
}

provider "aws" {
  region = var.aws_region
}

# Use the default VPC + subnets for dev simplicity
data "aws_vpc" "default" {
  default = true
}
data "aws_subnets" "default" {
  filter {
    name   = "vpc-id"
    values = [data.aws_vpc.default.id]
  }
}

# Security group: allow Postgres from your IP
resource "aws_security_group" "db_sg" {
  name        = "${var.project}-db-sg"
  description = "Allow Postgres from developer IP"
  vpc_id      = data.aws_vpc.default.id

  ingress {
    description = "psql from your IP"
    from_port   = 5432
    to_port     = 5432
    protocol    = "tcp"
    cidr_blocks = [var.allowed_cidr]
  }

  egress {
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }
}

resource "aws_db_subnet_group" "db" {
  name       = "${var.project}-db-subnets"
  subnet_ids = data.aws_subnets.default.ids
}

# RDS PostgreSQL (dev-friendly: publicly accessible, no final snapshot)
resource "aws_db_instance" "pg" {
  identifier               = "${var.project}-pg"
  engine                   = "postgres"
  engine_version           = "16.3"
  instance_class           = "db.t4g.micro"     # t3.micro if your account is Intel-only
  allocated_storage        = 20
  db_name                  = var.db_name
  username                 = var.db_user
  password                 = var.db_password
  port                     = 5432

  db_subnet_group_name     = aws_db_subnet_group.db.name
  vpc_security_group_ids   = [aws_security_group.db_sg.id]

  publicly_accessible      = true              # dev only
  skip_final_snapshot      = true              # dev only

  deletion_protection      = false
}

# Optional S3 bucket for raw SEG-Y files (you can wire this in later)
resource "random_id" "suffix" { byte_length = 3 }

resource "aws_s3_bucket" "raw" {
  bucket        = "${var.project}-segy-raw-${random_id.suffix.hex}"
  force_destroy = true
}

output "db_endpoint"  { value = aws_db_instance.pg.address }
output "db_port"      { value = aws_db_instance.pg.port }
output "s3_bucket"    { value = aws_s3_bucket.raw.bucket }
