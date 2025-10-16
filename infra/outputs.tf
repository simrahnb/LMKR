output "connection_uri" {
  value     = "postgresql://${var.db_user}:${var.db_password}@${aws_db_instance.pg.address}:${aws_db_instance.pg.port}/${var.db_name}"
  sensitive = true
}
