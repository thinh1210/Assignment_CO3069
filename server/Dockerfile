# Sử dụng Python nhẹ nhất
FROM python:3.12-slim

# Đặt thư mục làm việc
WORKDIR /app

# Copy file requirements và cài thư viện
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy toàn bộ code vào
COPY . .

# Mở cổng 3000
EXPOSE 8000

# Lệnh chạy server
CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]