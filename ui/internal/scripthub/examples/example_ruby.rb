text = <<~TEXT
  zeri engine script hub zeri
  script history versioning script
TEXT

counts = Hash.new(0)
text.downcase.scan(/\b[a-z]+\b/).each { |word| counts[word] += 1 }

counts.sort_by { |word, count| [-count, word] }.each do |word, count|
  puts "#{word}: #{count}"
end
