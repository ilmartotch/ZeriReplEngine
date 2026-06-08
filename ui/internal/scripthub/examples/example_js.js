const users = [
  { name: "Luca", active: true, score: 19 },
  { name: "Sara", active: false, score: 14 },
  { name: "Marta", active: true, score: 22 }
];

const summary = users
  .filter((user) => user.active)
  .map((user) => `${user.name}:${user.score}`)
  .join(", ");

console.log(summary);
